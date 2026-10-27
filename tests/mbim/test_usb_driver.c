#include "mbim.h"
#include "mbim_usb.h"
#include "device/usbd_pvt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static emu_mbim core;
static emu_modem modem;
static const usbd_class_driver_t *driver;
static void *control_buffer;
static uint16_t control_length;
static bool busy[256],endpoint_open[256];
static bool reject_next_xfer;
static uint8_t *buffers[256];
static uint16_t transfers[256];
static unsigned host_rx;
static uint32_t get32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);}
size_t emu_mbim_command(const uint8_t *q,size_t n,uint8_t *o,size_t cap){return emu_mbim_request(&core,&modem,q,n,o,cap,100);}
bool emu_mbim_command_pending(void){return core.activation.pending||core.scan_pending;}
size_t emu_mbim_events(uint8_t *o,size_t cap){return emu_mbim_poll(&core,&modem,o,cap,101);}
void emu_mbim_host_reset(void){emu_mbim_init(&core);modem.requested=false;}
void emu_mbim_wire_stats(bool from_host,size_t n){if(from_host)core.tx_bytes+=n;else core.rx_bytes+=n;}
bool emu_mbim_host_ip(const uint8_t *ip,size_t n){assert(n==20&&ip[0]==0x45);host_rx++;return true;}
bool tud_control_xfer(uint8_t p,const tusb_control_request_t *r,void *b,uint16_t n){(void)p;(void)r;control_buffer=b;control_length=n;return true;}
bool tud_control_status(uint8_t p,const tusb_control_request_t *r){(void)p;(void)r;control_buffer=NULL;control_length=0;return true;}
bool usbd_edpt_open(uint8_t p,const tusb_desc_endpoint_t *ep){(void)p;endpoint_open[ep->bEndpointAddress]=true;return true;}
void usbd_edpt_close(uint8_t p,uint8_t ep){(void)p;endpoint_open[ep]=false;busy[ep]=false;}
bool usbd_edpt_xfer(uint8_t p,uint8_t ep,uint8_t *b,uint16_t n,bool isr){(void)p;assert(!isr&&endpoint_open[ep]&&!busy[ep]);if(reject_next_xfer){reject_next_xfer=false;return false;}busy[ep]=true;buffers[ep]=b;transfers[ep]=n;return true;}
bool usbd_edpt_busy(uint8_t p,uint8_t ep){(void)p;return busy[ep];}
bool tud_mounted(void){return true;}
bool tud_connected(void){return true;}
static bool setup(uint8_t type,uint8_t req,uint16_t value,uint16_t index,uint16_t length){tusb_control_request_t r={type,req,value,index,length};return driver->control_xfer_cb(0,CONTROL_STAGE_SETUP,&r);}
static void send(const uint8_t *q,size_t n){
    tusb_control_request_t r={0x21,0,0,0,(uint16_t)n};assert(driver->control_xfer_cb(0,CONTROL_STAGE_SETUP,&r));assert(control_buffer&&control_length==n);memcpy(control_buffer,q,n);
    assert(driver->control_xfer_cb(0,CONTROL_STAGE_DATA,&r));assert(driver->control_xfer_cb(0,CONTROL_STAGE_ACK,&r));
}
static void complete(uint8_t ep,uint32_t n){assert(busy[ep]);busy[ep]=false;assert(driver->xfer_cb(0,ep,XFER_RESULT_SUCCESS,n));}
static uint8_t config[78]={[0]=9,[1]=4,[5]=2,[6]=14,[39]=7,[40]=5,[41]=0x81,[42]=3,[43]=8,[45]=16,[64]=7,[65]=5,[66]=2,[67]=2,[68]=64,[71]=7,[72]=5,[73]=0x82,[74]=2,[75]=64};
static void initialize(void){
    emu_init(&modem);memset(busy,0,sizeof(busy));memset(endpoint_open,0,sizeof(endpoint_open));uint8_t count;
    driver=usbd_app_driver_get_cb(&count);assert(count==1&&!strcmp(driver->name,"MBIM"));driver->init();
    assert(driver->open(0,(tusb_desc_interface_t *)config,78)==78&&endpoint_open[0x81]);
}
static void control_requests(void){
    initialize();assert(!driver->open(0,(tusb_desc_interface_t *)config,77));config[6]=2;assert(!driver->open(0,(tusb_desc_interface_t *)config,78));config[6]=14;
    assert(setup(0x81,10,0,1,1)&&control_length==1&&*(uint8_t *)control_buffer==0);
    assert(!setup(1,11,2,1,0));assert(!setup(1,11,0,2,0));assert(!setup(1,11,1,0,0));
    assert(setup(1,11,1,1,0)&&endpoint_open[2]&&endpoint_open[0x82]&&busy[2]);
    assert(setup(0x81,10,0,1,1)&&*(uint8_t *)control_buffer==1);
    assert(setup(0xa1,0x80,0,0,28)&&control_length==28&&get32((uint8_t *)control_buffer+4)==2048);
    assert(setup(0xa1,0x83,0,0,2)&&control_length==2);assert(setup(0xa1,0x85,0,0,4)&&get32(control_buffer)==2048);
    assert(setup(0xa1,0x87,0,0,2)&&control_length==2);assert(setup(0xa1,0x89,0,0,2)&&control_length==2);
    assert(setup(0x21,0x84,0,0,0));assert(!setup(0x21,0x84,1,0,0));assert(setup(0x21,0x8a,0,0,0));
    assert(!setup(0x21,0,0,0,1025));assert(!setup(0x21,0,0,0,0));assert(!setup(0x21,0x86,0,0,5));
    assert(!setup(0xa1,0x80,0,1,28));assert(!setup(0xa1,0xfe,0,0,1));assert(!setup(0x21,0xfe,0,0,1));
    assert(setup(0x21,0x43,15,0,0));assert(!setup(0x21,0x43,15,0,1));
    tusb_control_request_t ordinary={0xa1,0x80,0,0,28};assert(driver->control_xfer_cb(0,CONTROL_STAGE_DATA,&ordinary));
    tusb_control_request_t r={0x21,0x86,0,0,8};assert(driver->control_xfer_cb(0,0,&r));put32(control_buffer,2048);assert(driver->control_xfer_cb(0,1,&r));
    put32(control_buffer,1024);assert(!driver->control_xfer_cb(0,1,&r));
    r.bRequest=0x88;r.wLength=2;assert(driver->control_xfer_cb(0,0,&r));((uint8_t *)control_buffer)[0]=0xdc;((uint8_t *)control_buffer)[1]=5;assert(driver->control_xfer_cb(0,1,&r));
    assert(setup(1,11,0,1,0)&&!endpoint_open[2]);assert(setup(1,11,0,0,0));
}
static void response_queue(void){
    initialize();uint8_t q[52]={1,0,0,0,16,0,0,0,1,0,0,0,0,4,0,0};send(q,16);assert(core.opened&&busy[0x81]&&transfers[0x81]==8);
    complete(0x81,8);assert(!setup(0xa1,1,0,0,8));assert(setup(0xa1,1,0,0,1024));assert(control_length==16&&get32(control_buffer)==0x80000001);
    tusb_control_request_t get={0xa1,1,0,0,1024};assert(driver->control_xfer_cb(0,CONTROL_STAGE_ACK,&get));assert(setup(0xa1,1,0,0,1024)&&!control_length);
    memset(q,0,48);put32(q,3);put32(q+4,48);put32(q+8,2);put32(q+12,1);memcpy(q+20,emu_mbim_basic_uuid,16);put32(q+36,1);
    for(unsigned i=0;i<4;i++)send(q,48);
    assert(!setup(0x21,0,0,0,48));
    for(unsigned i=0;i<4;i++){
        if(busy[0x81])complete(0x81,8);
        assert(setup(0xa1,1,0,0,1024)&&control_length>48&&get32(control_buffer)==0x80000003);
        assert(driver->control_xfer_cb(0,CONTROL_STAGE_ACK,&get));
    }
    assert(setup(0xa1,1,0,0,1024)&&!control_length);
    emu_mbim_usb_poll();char diag[256];emu_mbim_usb_diag(diag,sizeof(diag));assert(strstr(diag,"last_cid=1")&&strstr(diag,"OK"));
    put32(q+4,52);put32(q+36,15);put32(q+44,4);put32(q+48,0);send(q,52);
    if(busy[0x81])complete(0x81,8);
    assert(setup(0xa1,1,0,0,1024)&&control_length==48);assert(driver->control_xfer_cb(0,CONTROL_STAGE_ACK,&get));
    char journal[256];emu_mbim_usb_trace(0,journal,sizeof(journal));assert(strstr(journal,"event=4")&&strstr(journal,"cid=15"));
    emu_mbim_usb_trace(64,journal,sizeof(journal));assert(strstr(journal,"ERROR"));
}
static void trace_metadata(void){
    initialize();char journal[256];
    uint8_t error[16]={4,0,0,0,16,0,0,0,0,0,0,0,1,0,0,0};send(error,sizeof(error));
    emu_mbim_usb_trace(0,journal,sizeof(journal));assert(strstr(journal,"type=4")&&strstr(journal,"field=1"));
    for(unsigned i=0;i<70;i++)send(error,sizeof(error));
    emu_mbim_usb_trace(63,journal,sizeof(journal));assert(strstr(journal,"OK")&&strstr(journal,"type=4"));
    assert(emu_mbim_usb_trace_command("AT+EMUMBIMTRACE=0",journal,sizeof(journal))&&strstr(journal,"type=4"));
    assert(!emu_mbim_usb_trace_command("ATI",journal,sizeof(journal)));
    const char *bad[]={"AT+EMUMBIMTRACE=","AT+EMUMBIMTRACE=-1","AT+EMUMBIMTRACE=+1","AT+EMUMBIMTRACE=0x1","AT+EMUMBIMTRACE=64","AT+EMUMBIMTRACE=99999999999999999999999"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(emu_mbim_usb_trace_command(bad[i],journal,sizeof(journal))&&strstr(journal,"ERROR"));
    assert(driver->deinit());
}
static void reopen_session(void){
    initialize();uint8_t open[16]={1,0,0,0,16,0,0,0,1,0,0,0,0,4,0,0};
    send(open,16);uint8_t q[48]={3,0,0,0,48,0,0,0,2,0,0,0,1,0,0,0};
    memcpy(q+20,emu_mbim_basic_uuid,16);put32(q+36,1);
    for(unsigned i=0;i<3;i++)send(q,48);
    assert(!setup(0x21,0,0,0,48));
    send(open,16);assert(setup(0xa1,1,0,0,1024)&&control_length==16&&get32(control_buffer)==0x80000001);
    tusb_control_request_t get={0xa1,1,0,0,1024};assert(driver->control_xfer_cb(0,CONTROL_STAGE_ACK,&get));
    assert(setup(0xa1,1,0,0,1024)&&control_length==0);
    uint8_t close[12]={2,0,0,0,12,0,0,0,3,0,0,0};send(close,12);
    assert(setup(0xa1,1,0,0,1024)&&get32(control_buffer)==0x80000002);
    assert(driver->control_xfer_cb(0,CONTROL_STAGE_ACK,&get));assert(!core.opened);
}
static void trace_services(void){
    initialize();uint8_t open[16]={1,0,0,0,16,0,0,0,1,0,0,0,0,4,0,0};send(open,16);
    uint8_t q[68]={3,0,0,0,48,0,0,0,2,0,0,0,1,0,0,0};put32(q+36,1);
    char journal[256];memcpy(q+20,emu_mbim_atds_uuid,16);send(q,48);emu_mbim_usb_trace(0,journal,sizeof(journal));assert(strstr(journal,"service=3"));
    memset(q+20,0,16);send(q,48);emu_mbim_usb_trace(0,journal,sizeof(journal));assert(strstr(journal,"service=4"));
    memcpy(q+20,emu_mbim_ms_uuid,16);put32(q+36,11);put32(q+4,68);put32(q+44,20);send(q,68);
    emu_mbim_usb_trace(0,journal,sizeof(journal));assert(strstr(journal,"service=2"));
}
static bool ip_received(const uint8_t *p,size_t n,void *c){(void)c;return n>=20&&p[0]==0x45;}
static void data_channel(void){
    initialize();assert(!emu_mbim_usb_can_xmit(34));assert(setup(1,11,1,1,0));
    uint8_t ip[100]={0x45,0,0,20},b[2048];size_t n=emu_mbim_ntb_send(b,sizeof(b),ip,20,0);memcpy(buffers[2],b,n);complete(2,(uint32_t)n);assert(host_rx==1&&busy[2]);
    assert(emu_mbim_usb_can_xmit(34));assert(!emu_mbim_usb_can_xmit(1515));uint8_t frame[114]={0};frame[12]=8;memcpy(frame+14,ip,20);
    emu_mbim_usb_xmit(frame,34);assert(busy[0x82]&&transfers[0x82]==48);assert(emu_mbim_ntb_receive(buffers[0x82],48,ip_received,NULL));complete(0x82,48);
    frame[16]=0;frame[17]=100;emu_mbim_usb_xmit(frame,114);assert(transfers[0x82]==128);complete(0x82,128);assert(busy[0x82]&&transfers[0x82]==0);complete(0x82,0);
    frame[13]=6;emu_mbim_usb_xmit(frame,34);assert(!busy[0x82]);assert(driver->deinit());
    initialize();assert(setup(1,11,1,1,0));frame[13]=0;frame[17]=20;reject_next_xfer=true;
    emu_mbim_usb_xmit(frame,34);assert(!busy[0x82]);assert(driver->deinit());
}
int main(void){char journal[256];emu_mbim_usb_trace(0,journal,sizeof(journal));assert(strstr(journal,"ERROR"));control_requests();response_queue();trace_metadata();reopen_session();trace_services();data_channel();puts("PASS actual MBIM USB driver: control/queue/NTB/ZLP host harness (not hardware)");return 0;}
