/* Physical USB/Wi-Fi adapter. Protocol parsing stays in the host-tested modem component. */
#include "modem.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "tusb.h"
#include "device/usbd_pvt.h"
#if CONFIG_EMU_MBIM
#include "mbim.h"
#include "mbim_usb.h"
#include "lwip/etharp.h"
#endif
#if CONFIG_EMU_AT_USB
#include "tinyusb_cdc_acm.h"
#endif

void emu_usb_start(void);
uint8_t tud_network_mac_address[6]={0x02,0x12,0xd1,0x14,0xdb,0x02};
static const uint8_t gateway_mac[6]={0x02,0x12,0xd1,0x14,0xdb,0x01};
static emu_modem modem;
static SemaphoreHandle_t lock;
static esp_netif_t *usb_netif,*wifi_netif;
/* ESP-NETIF requires a non-NULL, persistent driver handle even for callback-only IO. */
static uint8_t usb_driver_context;
typedef struct { uint16_t size; uint8_t data[1514]; } packet;
typedef struct { uint8_t size; uint8_t data[64]; } control_chunk;
static QueueHandle_t tx_queue,control_queue;
static atomic_bool poll_pending,control_overflow,changing_wifi;
static atomic_uint applied_generation;
static atomic_uint tx_dropped,rx_dropped;
static emu_activation pending[2];
#if CONFIG_EMU_MBIM
static emu_mbim mbim;
static bool wifi_scan_busy;
static uint32_t wifi_scan_token;
static emu_mbim_ap wifi_scan_aps[EMU_MBIM_SCAN_MAX];
static wifi_ap_record_t wifi_scan_records[EMU_MBIM_SCAN_MAX];
#endif

static emu_modem snapshot(void) {
    emu_modem m;
    xSemaphoreTake(lock,portMAX_DELAY);m=modem;xSemaphoreGive(lock);return m;
}
static bool frame_allowed(const uint8_t *p,size_t n,bool from_usb) {
    emu_modem m=snapshot();bool allowed=emu_frame_allowed(&m,p,n,from_usb);emu_wipe(&m,sizeof(m));return allowed;
}

static esp_err_t usb_transmit(void *handle,void *buffer,size_t len) {
    (void)handle;
    if (!frame_allowed(buffer,len,false)) {atomic_fetch_add(&tx_dropped,1);return ESP_FAIL;}
    packet p={.size=(uint16_t)len};memcpy(p.data,buffer,len);
    if (xQueueSend(tx_queue,&p,0)!=pdTRUE){atomic_fetch_add(&tx_dropped,1);return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
static void usb_free(void *handle,void *buffer){(void)handle;free(buffer);}

bool tud_network_recv_cb(const uint8_t *src,uint16_t size) {
    if (usb_netif && frame_allowed(src,size,true)) {
        void *copy=malloc(size);
        if (copy) {
            memcpy(copy,src,size);
            /* esp_netif owns and releases the accepted RX buffer. */
            if (esp_netif_receive(usb_netif,copy,size,NULL)!=ESP_OK) atomic_fetch_add(&rx_dropped,1);
        } else atomic_fetch_add(&rx_dropped,1);
    } else atomic_fetch_add(&rx_dropped,1);
#if !CONFIG_EMU_MBIM
    tud_network_recv_renew();
#endif
    return true;
}
uint16_t tud_network_xmit_cb(uint8_t *dst,void *ref,uint16_t arg){memcpy(dst,ref,arg);return arg;}
void tud_network_init_cb(void){if(tx_queue)xQueueReset(tx_queue);}

static void usb_poll(void *unused) {
    (void)unused;static packet next;
    if(!tud_mounted()){xQueueReset(tx_queue);atomic_store(&poll_pending,false);return;}
#if CONFIG_EMU_MBIM
    emu_mbim_usb_poll();
    bool can_xmit=xQueuePeek(tx_queue,&next,0)==pdTRUE && emu_mbim_usb_can_xmit(next.size);
#else
    bool can_xmit=xQueuePeek(tx_queue,&next,0)==pdTRUE && tud_network_can_xmit(next.size);
#endif
    if(can_xmit){
        xQueueReceive(tx_queue,&next,0);
        if(frame_allowed(next.data,next.size,false)){
#if CONFIG_EMU_MBIM
            emu_mbim_usb_xmit(next.data,next.size);
#else
            tud_network_xmit(next.data,next.size);
#endif
        }
        else atomic_fetch_add(&tx_dropped,1);
    }
    atomic_store(&poll_pending,false);
}

#if CONFIG_EMU_MBIM
size_t emu_mbim_command(const uint8_t *q,size_t n,uint8_t *o,size_t cap){
    xSemaphoreTake(lock,portMAX_DELAY);
    size_t result=emu_mbim_request(&mbim,&modem,q,n,o,cap,(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS));
    xSemaphoreGive(lock);return result;
}
bool emu_mbim_command_pending(void){xSemaphoreTake(lock,portMAX_DELAY);bool p=mbim.activation.pending||mbim.scan_pending;xSemaphoreGive(lock);return p;}
size_t emu_mbim_events(uint8_t *o,size_t cap){
    xSemaphoreTake(lock,portMAX_DELAY);
    size_t result=emu_mbim_poll(&mbim,&modem,o,cap,(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS));
    xSemaphoreGive(lock);return result;
}
void emu_mbim_host_reset(void){
    xSemaphoreTake(lock,portMAX_DELAY);emu_mbim_init(&mbim);modem.requested=false;xSemaphoreGive(lock);
}
void emu_mbim_wire_stats(bool from_host,size_t n){
    xSemaphoreTake(lock,portMAX_DELAY);
    if(from_host){mbim.tx_bytes+=n;mbim.tx_packets++;}else{mbim.rx_bytes+=n;mbim.rx_packets++;}
    xSemaphoreGive(lock);
}
bool emu_mbim_host_ip(const uint8_t *ip,size_t n){
    static uint8_t ethernet[1514];
    if(n>1500)return false;
    memcpy(ethernet,gateway_mac,6);memcpy(ethernet+6,tud_network_mac_address,6);
    ethernet[12]=8;ethernet[13]=0;memcpy(ethernet+14,ip,n);
    return tud_network_recv_cb(ethernet,(uint16_t)(14+n));
}
static esp_err_t mbim_neighbor(void *unused){
    (void)unused;ip4_addr_t ip;IP4_ADDR(&ip,192,0,2,2);struct eth_addr mac;
    memcpy(mac.addr,tud_network_mac_address,6);
    return etharp_add_static_entry(&ip,&mac)==ERR_OK?ESP_OK:ESP_FAIL;
}
#endif

static void network_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_ip_info_t ip={0};
    IP4_ADDR(&ip.ip,192,0,2,1);IP4_ADDR(&ip.gw,192,0,2,1);IP4_ADDR(&ip.netmask,255,255,255,0);
    esp_netif_inherent_config_t base=ESP_NETIF_INHERENT_DEFAULT_ETH();
    base.flags=ESP_NETIF_DHCP_SERVER|ESP_NETIF_FLAG_AUTOUP;
    base.if_key="USB_RNDIS";base.if_desc="USB modem";base.route_prio=0;base.ip_info=&ip;
    memcpy(base.mac,gateway_mac,sizeof(gateway_mac));
    esp_netif_driver_ifconfig_t driver={.handle=&usb_driver_context,.transmit=usb_transmit,.driver_free_rx_buffer=usb_free};
    esp_netif_config_t cfg={.base=&base,.driver=&driver,.stack=ESP_NETIF_NETSTACK_DEFAULT_ETH};
    usb_netif=esp_netif_new(&cfg);assert(usb_netif);
    ESP_ERROR_CHECK(esp_netif_set_mac(usb_netif,base.mac));
    esp_netif_dns_info_t dns={0};dns.ip.type=ESP_IPADDR_TYPE_V4;IP4_ADDR(&dns.ip.u_addr.ip4,1,1,1,1);
    ESP_ERROR_CHECK(esp_netif_set_dns_info(usb_netif,ESP_NETIF_DNS_MAIN,&dns));
    uint8_t offer=1;
    ESP_ERROR_CHECK(esp_netif_dhcps_option(usb_netif,ESP_NETIF_OP_SET,ESP_NETIF_DOMAIN_NAME_SERVER,&offer,sizeof(offer)));
    esp_netif_action_start(usb_netif,NULL,0,NULL);esp_netif_action_connected(usb_netif,NULL,0,NULL);
    ESP_ERROR_CHECK(esp_netif_napt_enable(usb_netif));
#if CONFIG_EMU_MBIM
    /* MBIM is raw IP, with no ARP/DHCP exchanges from the host. The internal
       Ethernet shim needs a static neighbor for the fixed MBIM session address. */
    ESP_ERROR_CHECK(esp_netif_tcpip_exec(mbim_neighbor,NULL));
#endif
    wifi_netif=esp_netif_create_default_wifi_sta();assert(wifi_netif);
}

static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;
#if CONFIG_EMU_MBIM
    if(base==WIFI_EVENT && id==WIFI_EVENT_SCAN_DONE){
        wifi_event_sta_scan_done_t *done=data;uint16_t count=EMU_MBIM_SCAN_MAX;
        bool ok=done->status==0&&esp_wifi_scan_get_ap_records(&count,wifi_scan_records)==ESP_OK;
        if(!ok){count=0;(void)esp_wifi_clear_ap_list();}
        for(size_t i=0;i<count;i++){
            memset(&wifi_scan_aps[i],0,sizeof(wifi_scan_aps[i]));
            memcpy(wifi_scan_aps[i].ssid,wifi_scan_records[i].ssid,32);
            wifi_scan_aps[i].rssi=wifi_scan_records[i].rssi;wifi_scan_aps[i].channel=wifi_scan_records[i].primary;
            memcpy(wifi_scan_aps[i].bssid,wifi_scan_records[i].bssid,6);
        }
        xSemaphoreTake(lock,portMAX_DELAY);
        emu_mbim_scan_complete(&mbim,wifi_scan_token,wifi_scan_aps,count,ok);wifi_scan_busy=false;
        xSemaphoreGive(lock);
    }
#endif
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED){
        xSemaphoreTake(lock,portMAX_DELAY);modem.wifi_ip=false;
#if CONFIG_EMU_MBIM
        emu_mbim_current_ap(&mbim,NULL);
#endif
        bool retry=modem.configured&&modem.radio_on&&modem.sim_ready;xSemaphoreGive(lock);
        if(retry&&!atomic_load(&changing_wifi)) (void)esp_wifi_connect();
    }
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP){
        ip_event_got_ip_t *event=data;wifi_ap_record_t ap={0};
        bool identity=esp_wifi_sta_get_ap_info(&ap)==ESP_OK;
        uint32_t addr=ntohl(event->ip_info.ip.addr), mask=ntohl(event->ip_info.netmask.addr);
        bool conflict=((addr&mask)==(0xc0000201u&mask)) || ((addr&0xffffff00u)==0xc0000200u);
        xSemaphoreTake(lock,portMAX_DELAY);
        bool match=identity&&modem.configured&&modem.generation==atomic_load(&applied_generation)&&
                   !memcmp(ap.ssid,modem.wifi.ssid,modem.wifi.ssid_len)&&
                   (modem.wifi.ssid_len==32 || ap.ssid[modem.wifi.ssid_len]==0);
        modem.wifi_ip=match&&!conflict;xSemaphoreGive(lock);
        if(match&&!conflict){
            esp_netif_dns_info_t dns;
            if(esp_netif_get_dns_info(wifi_netif,ESP_NETIF_DNS_MAIN,&dns)==ESP_OK&&dns.ip.u_addr.ip4.addr)
                (void)esp_netif_set_dns_info(usb_netif,ESP_NETIF_DNS_MAIN,&dns);
        }
    }
    if(base==IP_EVENT && id==IP_EVENT_STA_LOST_IP){xSemaphoreTake(lock,portMAX_DELAY);modem.wifi_ip=false;xSemaphoreGive(lock);}
}

#if CONFIG_EMU_MBIM
static void wifi_signal_poll(void){
    static TickType_t next;TickType_t now=xTaskGetTickCount();
    if((int32_t)(now-next)<0)return;
    next=now+pdMS_TO_TICKS(2000);
    wifi_ap_record_t record;emu_mbim_ap ap={0};bool valid=esp_wifi_sta_get_ap_info(&record)==ESP_OK;
    if(valid){memcpy(ap.ssid,record.ssid,32);memcpy(ap.bssid,record.bssid,6);ap.channel=record.primary;ap.rssi=record.rssi;}
    xSemaphoreTake(lock,portMAX_DELAY);emu_mbim_current_ap(&mbim,valid?&ap:NULL);xSemaphoreGive(lock);
}
static void wifi_scan_poll(void){
    xSemaphoreTake(lock,portMAX_DELAY);
    bool start=!wifi_scan_busy&&mbim.scan_pending&&!mbim.scan_started;
    if(start){if(++wifi_scan_token==0)++wifi_scan_token;start=emu_mbim_scan_take(&mbim,wifi_scan_token);wifi_scan_busy=start;}
    xSemaphoreGive(lock);
    if(start){
        wifi_scan_config_t config={.show_hidden=true,.scan_type=WIFI_SCAN_TYPE_ACTIVE};
        if(esp_wifi_scan_start(&config,false)!=ESP_OK){
            xSemaphoreTake(lock,portMAX_DELAY);emu_mbim_scan_complete(&mbim,wifi_scan_token,NULL,0,false);wifi_scan_busy=false;xSemaphoreGive(lock);
        }
    }
}
#endif
static esp_err_t persist(const emu_modem *m,bool forget) {
    nvs_handle_t h;esp_err_t err=nvs_open("emu_wifi",NVS_READWRITE,&h);
    if(err!=ESP_OK)return err;
    if(forget){err=nvs_erase_key(h,"profile1");if(err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;}
    else{
        emu_profile old={0};size_t n=sizeof(old);
        if(nvs_get_blob(h,"profile1",&old,&n)==ESP_OK&&n==sizeof(old)&&!memcmp(&old,&m->active,n)){
            emu_wipe(&old,sizeof(old));nvs_close(h);return ESP_OK;
        }
        emu_wipe(&old,sizeof(old));err=nvs_set_blob(h,"profile1",&m->active,sizeof(m->active));
    }
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);return err;
}

static void load_profile(void){
    nvs_handle_t h;if(nvs_open("emu_wifi",NVS_READONLY,&h)!=ESP_OK)return;
    emu_profile p={0};size_t n=sizeof(p);
    if(nvs_get_blob(h,"profile1",&p,&n)==ESP_OK&&n==sizeof(p)&&emu_set_profile(&modem,&p)){
        modem.staged=p;modem.requested=true;
    }
    emu_wipe(&p,sizeof(p));nvs_close(h);
}

static void wifi_apply(void) {
    emu_modem m=snapshot();uint32_t applied=atomic_load(&applied_generation);
    if(!m.configured||!m.radio_on||!m.sim_ready){
        if(applied){atomic_store(&changing_wifi,true);(void)esp_wifi_disconnect();atomic_store(&applied_generation,0);atomic_store(&changing_wifi,false);}
    }else if(m.generation!=applied){
        atomic_store(&changing_wifi,true);(void)esp_wifi_disconnect();wifi_config_t cfg={0};
        memcpy(cfg.sta.ssid,m.wifi.ssid,m.wifi.ssid_len);memcpy(cfg.sta.password,m.wifi.password,strlen(m.wifi.password));
        cfg.sta.threshold.authmode=m.active.auth==0?WIFI_AUTH_OPEN:WIFI_AUTH_WPA2_PSK;
        esp_err_t err=esp_wifi_set_config(WIFI_IF_STA,&cfg);emu_wipe(&cfg,sizeof(cfg));
        if(err==ESP_OK){atomic_store(&applied_generation,m.generation);(void)esp_wifi_connect();}
        atomic_store(&changing_wifi,false);
    }
    emu_wipe(&m,sizeof(m));
}

static void respond(bool usb,const char *s){
#if CONFIG_EMU_AT_USB
    if(usb){tinyusb_cdcacm_write_queue(0,(const uint8_t *)s,strlen(s));(void)tinyusb_cdcacm_write_flush(0,0);return;}
#else
    (void)usb;
#endif
    uart_write_bytes(UART_NUM_0,s,strlen(s));
}
static void accept_byte(emu_line *line,uint8_t byte,bool usb){
    /* Both transports are parsed serially by app_main. Keep long-lived buffers
       off its stack: profile parsing plus libc exceeded the old 3584B budget. */
    static char cmd[EMU_AT_LINE_MAX+1],out[EMU_AT_RESPONSE_MAX];emu_action action;
    if(!emu_line_byte(line,byte,cmd,sizeof(cmd)))return;
#if CONFIG_EMU_MBIM
    if(emu_mbim_usb_trace_command(cmd,out,sizeof(out))){
        respond(usb,out);emu_wipe(cmd,sizeof(cmd));emu_wipe(out,sizeof(out));return;
    }
    if(!strcmp(cmd,"AT+EMUMBIM?")){
        emu_mbim_usb_diag(out,sizeof(out));respond(usb,out);emu_wipe(cmd,sizeof(cmd));emu_wipe(out,sizeof(out));return;
    }
#endif
    if(!strcmp(cmd,"AT+EMURES?")){
        snprintf(out,sizeof(out),"\r\n+EMURES: reset=%u,heap=%lu,main_stack_free_min=%lu\r\nOK\r\n",
                 (unsigned)esp_reset_reason(),(unsigned long)esp_get_free_heap_size(),
                 (unsigned long)uxTaskGetStackHighWaterMark(NULL));
        respond(usb,out);emu_wipe(cmd,sizeof(cmd));emu_wipe(out,sizeof(out));return;
    }
    if(pending[usb?1:0].pending){emu_wipe(cmd,sizeof(cmd));respond(usb,"\r\nERROR\r\n");return;}
    xSemaphoreTake(lock,portMAX_DELAY);bool ok=emu_at(&modem,cmd,out,sizeof(out),&action);xSemaphoreGive(lock);
    emu_wipe(cmd,sizeof(cmd));
    if(ok&&action==EMU_APPLY_WIFI){
        emu_modem m=snapshot();
        if(!emu_session_ready(&m)){
            emu_activation_begin(&pending[usb?1:0],&m,(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS));
            emu_wipe(&m,sizeof(m));emu_wipe(out,sizeof(out));return;
        }
        emu_wipe(&m,sizeof(m));
    }
    if(ok&&(action==EMU_SAVE||action==EMU_FORGET)){
        emu_modem m=snapshot();esp_err_t err=persist(&m,action==EMU_FORGET);emu_wipe(&m,sizeof(m));
        if(err!=ESP_OK)strcpy(out,"\r\nERROR\r\n");
    }
    if(ok)respond(usb,out);
    emu_wipe(out,sizeof(out));
}

#if CONFIG_EMU_AT_USB
static void cdc_receive(int itf,cdcacm_event_t *event){
    (void)event;control_chunk chunk;size_t n=0;
    if(tinyusb_cdcacm_read(itf,chunk.data,sizeof(chunk.data),&n)!=ESP_OK||!n)return;
    chunk.size=(uint8_t)n;
    if(xQueueSend(control_queue,&chunk,0)!=pdTRUE)atomic_store(&control_overflow,true);
    emu_wipe(&chunk,sizeof(chunk));
}
#endif

void app_main(void){
    emu_init(&modem);lock=xSemaphoreCreateMutex();assert(lock);
    tx_queue=xQueueCreate(4,sizeof(packet));control_queue=xQueueCreate(8,sizeof(control_chunk));assert(tx_queue&&control_queue);
    /* No automatic erase: unknown NVS version must not silently destroy user state. */
    ESP_ERROR_CHECK(nvs_flash_init());load_profile();network_init();
    wifi_init_config_t wifi_cfg=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,wifi_event,NULL));
#if CONFIG_EMU_MBIM
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_SCAN_DONE,wifi_event,NULL));
#endif
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_LOST_IP,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0,512,0,0,NULL,0));
    emu_usb_start();
#if CONFIG_EMU_AT_USB
    const tinyusb_config_cdcacm_t acm={.cdc_port=TINYUSB_CDC_ACM_0,.callback_rx=cdc_receive};
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm));
#endif
    static emu_line uart_line,cdc_line;uint8_t bytes[64];
    const char *greeting="\r\nESP-LTE ready; use AT commands; settings are RAM until AT&W\r\n";
    uart_write_bytes(UART_NUM_0,greeting,strlen(greeting));
    for(;;){
        int n=uart_read_bytes(UART_NUM_0,bytes,sizeof(bytes),pdMS_TO_TICKS(10));
        for(int i=0;i<n;i++)accept_byte(&uart_line,bytes[i],false);
        emu_wipe(bytes,sizeof(bytes));
        if(atomic_exchange(&control_overflow,false)){xQueueReset(control_queue);emu_wipe(&cdc_line,sizeof(cdc_line));cdc_line.overflow=true;}
        control_chunk chunk;
        for(unsigned budget=0;budget<4;budget++){
            if(xQueueReceive(control_queue,&chunk,0)!=pdTRUE)break;
            for(unsigned i=0;i<chunk.size;i++)accept_byte(&cdc_line,chunk.data[i],true);
            emu_wipe(&chunk,sizeof(chunk));
        }
        wifi_apply();
#if CONFIG_EMU_MBIM
        wifi_scan_poll();wifi_signal_poll();
#endif
        for(unsigned port=0;port<2;port++){
            xSemaphoreTake(lock,portMAX_DELAY);
            emu_activation_result status=emu_activation_poll(&pending[port],&modem,(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS));
            xSemaphoreGive(lock);
            if(status==EMU_ACT_OK)respond(port==1,"\r\nOK\r\n");
            if(status==EMU_ACT_ERROR)respond(port==1,"\r\n+CME ERROR: 30\r\n");
        }
        if(!atomic_exchange(&poll_pending,true))usbd_defer_func(usb_poll,NULL,false);
    }
}
