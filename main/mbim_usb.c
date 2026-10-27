#include "sdkconfig.h"
#if CONFIG_EMU_MBIM
#include "mbim_usb.h"
#include "mbim.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tusb.h"
#include "device/usbd_pvt.h"

/* All endpoint and queue state belongs to TinyUSB's task, never a Wi-Fi callback. */
enum { EP_NOTIFY=0x81, EP_OUT=0x02, EP_IN=0x82, REPLIES=4, DRIVER_LENGTH=78 };
static uint8_t rh,alt,head,count;
static bool get_active,notified,tx_zlp;
static uint16_t sequence;
static const tusb_desc_endpoint_t *bulk_out,*bulk_in;
static TU_ATTR_ALIGNED(4) uint8_t control[EMU_MBIM_CONTROL_MAX],rx[EMU_MBIM_NTB_MAX],tx[EMU_MBIM_NTB_MAX];
static TU_ATTR_ALIGNED(4) uint8_t replies[REPLIES][EMU_MBIM_CONTROL_MAX];
static uint16_t lengths[REPLIES];
static uint8_t notification[]={0xa1,1,0,0,0,0,0,0};
static atomic_uint control_count,last_request,last_cid,last_status,rx_packets,tx_packets;
static atomic_uint opens,resets,last_open_length;
static atomic_uint last_operation,last_info_length,last_session;
enum { TRACE_COUNT=64, TRACE_WORDS=8 };
static atomic_uint trace_cursor,trace_records[TRACE_COUNT][TRACE_WORDS];
static const uint8_t parameters[]={28,0,1,0,0,8,0,0,4,0,0,0,4,0,0,0,0,8,0,0,4,0,0,0,4,0,4,0};
static uint16_t get16(const uint8_t *p){return p[0]|(uint16_t)p[1]<<8;}
static uint32_t get32(const uint8_t *p){return get16(p)|(uint32_t)get16(p+2)<<16;}

/* Metadata journal survives USB resets, lives only in RAM, and never copies
   InformationBuffer contents (APN/authentication may contain credentials). */
static void trace(unsigned event,const uint8_t *message,size_t n,unsigned extra){
    unsigned serial=atomic_load(&trace_cursor)+1,index=(serial-1)%TRACE_COUNT;
    unsigned type=n>=12?get32(message):0,tid=n>=12?get32(message+8):0;
    unsigned service=0,cid=0,field=0;
    if(n>=44&&(type==3||type==0x80000003u||type==0x80000007u)){
        service=!memcmp(message+20,emu_mbim_basic_uuid,16)?1:!memcmp(message+20,emu_mbim_ms_uuid,16)?2:!memcmp(message+20,emu_mbim_atds_uuid,16)?3:4;
        cid=get32(message+36);field=type==0x80000007u?0:get32(message+40);
    }else if(n>=16)field=get32(message+12);
    unsigned values[TRACE_WORDS]={serial,event,type,tid,service,cid,field,(unsigned)n};
    values[7]|=extra<<16;
    atomic_store(&trace_records[index][0],0);
    for(unsigned i=1;i<TRACE_WORDS;i++)atomic_store(&trace_records[index][i],values[i]);
    atomic_store(&trace_records[index][0],serial);atomic_store(&trace_cursor,serial);
}

static void queue_ready(size_t n){
    if(!n)return;
    trace(2,replies[(head+count)%REPLIES],n,0);
    lengths[(head+count)%REPLIES]=(uint16_t)n;
    count++;
}
static void notify(void){
    if(count&&!notified&&!usbd_edpt_busy(rh,EP_NOTIFY)){
        if(usbd_edpt_xfer(rh,EP_NOTIFY,notification,sizeof(notification),false))notified=true;
    }
}
void emu_mbim_usb_poll(void){
    if(count<REPLIES){size_t n=emu_mbim_events(replies[(head+count)%REPLIES],EMU_MBIM_CONTROL_MAX);queue_ready(n);}
    notify();
}
static void reset(uint8_t port){
    atomic_fetch_add(&resets,1);
    rh=port;alt=0;head=count=0;get_active=notified=tx_zlp=false;sequence=0;
    memset(control,0,sizeof(control));memset(replies,0,sizeof(replies));memset(lengths,0,sizeof(lengths));
    bulk_out=bulk_in=NULL;emu_mbim_host_reset();
}
static void init(void){reset(0);}
static bool deinit(void){reset(rh);return true;}
static uint16_t open_driver(uint8_t port,const tusb_desc_interface_t *itf,uint16_t max_len){
    atomic_fetch_add(&opens,1);atomic_store(&last_open_length,max_len);
    if(itf->bInterfaceClass!=2||itf->bInterfaceSubClass!=0x0e||itf->bInterfaceNumber!=0||max_len<DRIVER_LENGTH)return 0;
    const uint8_t *d=(const uint8_t *)itf;
    const tusb_desc_endpoint_t *ep=(const tusb_desc_endpoint_t *)(d+39);
    if(ep->bDescriptorType!=5||ep->bEndpointAddress!=EP_NOTIFY||!usbd_edpt_open(port,ep))return 0;
    rh=port;bulk_out=(const tusb_desc_endpoint_t *)(d+64);bulk_in=(const tusb_desc_endpoint_t *)(d+71);
    return DRIVER_LENGTH;
}
static bool change_alt(uint16_t value){
    if(value>1||!bulk_out||!bulk_in)return false;
    if(value==alt)return true;
    if(!value){usbd_edpt_close(rh,EP_OUT);usbd_edpt_close(rh,EP_IN);alt=0;tx_zlp=false;return true;}
    if(!usbd_edpt_open(rh,bulk_out)||!usbd_edpt_open(rh,bulk_in))return false;
    alt=1;return usbd_edpt_xfer(rh,EP_OUT,rx,sizeof(rx),false);
}
static bool control_xfer(uint8_t port,uint8_t stage,const tusb_control_request_t *r){
    if(stage==CONTROL_STAGE_ACK){
        if(r->bmRequestType==0xa1&&r->bRequest==1&&get_active){
            trace(4,replies[head],lengths[head],r->wLength);
            memset(replies[head],0,sizeof(replies[head]));lengths[head]=0;head=(head+1)%REPLIES;count--;get_active=false;notified=false;
            notify();
        }
        return true;
    }
    if(stage==CONTROL_STAGE_DATA){
        if(r->bmRequestType==0x21&&r->bRequest==0){
            trace(1,control,r->wLength,0);
            /* A fresh OPEN/CLOSE begins a new control session. Previously
               queued indications must never precede the new session reply. */
            if((r->wLength==16&&get32(control)==1)||(r->wLength==12&&get32(control)==2)){
                head=count=0;get_active=notified=false;memset(replies,0,sizeof(replies));memset(lengths,0,sizeof(lengths));
            }
            if(r->wLength>=48){
                atomic_store(&last_cid,get32(control+36));atomic_store(&last_operation,get32(control+40));atomic_store(&last_info_length,get32(control+44));
                if(r->wLength>=52&&(get32(control+36)==12||get32(control+36)==15))atomic_store(&last_session,get32(control+48));
            }
            size_t n=emu_mbim_command(control,r->wLength,replies[(head+count)%REPLIES],EMU_MBIM_CONTROL_MAX);
            if(n>=48)atomic_store(&last_status,get32(replies[(head+count)%REPLIES]+40));
            bool accepted=n>0||(r->wLength>=4&&get32(control)==4)||emu_mbim_command_pending();
            memset(control,0,sizeof(control));queue_ready(n);notify();return accepted;
        }
        if(r->bmRequestType==0x21&&r->bRequest==0x86){
            return get32(control)>=EMU_MBIM_NTB_MAX;
        }
        if(r->bmRequestType==0x21&&r->bRequest==0x88)return get16(control)==1500;
        return true;
    }
    if(stage!=CONTROL_STAGE_SETUP)return false;
    get_active=false;atomic_fetch_add(&control_count,1);atomic_store(&last_request,r->bRequest);
    if(r->bmRequestType==0x81&&r->bRequest==TUSB_REQ_GET_INTERFACE&&r->wIndex<=1){
        control[0]=r->wIndex==1?alt:0;return tud_control_xfer(port,r,control,1);
    }
    if(r->bmRequestType==0x01&&r->bRequest==TUSB_REQ_SET_INTERFACE){
        if(r->wLength||r->wIndex>1||(!r->wIndex&&r->wValue))return false;
        if(r->wIndex==1&&!change_alt(r->wValue))return false;
        return tud_control_status(port,r);
    }
    if(r->wIndex||(r->wValue&&r->bRequest!=0x84&&r->bRequest!=0x8a&&r->bRequest!=0x43))return false;
    if(r->bmRequestType==0x21){
        switch(r->bRequest){
        case 0:
            if(!r->wLength||r->wLength>sizeof(control)||(count==REPLIES&&r->wLength!=12&&r->wLength!=16))return false;
            return tud_control_xfer(port,r,control,r->wLength);
        case 0x84:case 0x8a:
            if(r->wValue||r->wLength)return false;
            return tud_control_status(port,r);
        case 0x86:
            if(r->wLength!=4&&r->wLength!=8)return false;
            return tud_control_xfer(port,r,control,r->wLength);
        case 0x88:
            if(r->wLength!=2)return false;
            return tud_control_xfer(port,r,control,2);
        case 0x43:
            if(r->wLength)return false;
            return tud_control_status(port,r);
        default:return false;
        }
    }
    if(r->bmRequestType!=0xa1)return false;
    switch(r->bRequest){
    case 1:
        trace(3,count?replies[head]:NULL,count?lengths[head]:0,r->wLength);
        if(count){if(r->wLength<lengths[head])return false;get_active=true;return tud_control_xfer(port,r,replies[head],lengths[head]);}
        return tud_control_xfer(port,r,NULL,0);
    case 0x80:return tud_control_xfer(port,r,(void *)(uintptr_t)parameters,sizeof(parameters));
    case 0x83:case 0x89:control[0]=control[1]=0;return tud_control_xfer(port,r,control,2);
    case 0x85:memcpy(control,(uint8_t[]){0,8,0,0},4);return tud_control_xfer(port,r,control,4);
    case 0x87:memcpy(control,(uint8_t[]){0xdc,5},2);return tud_control_xfer(port,r,control,2);
    default:return false;
    }
}
static bool receive_ip(const uint8_t *ip,size_t n,void *context){
    (void)context;bool accepted=emu_mbim_host_ip(ip,n);
    if(accepted){atomic_fetch_add(&rx_packets,1);emu_mbim_wire_stats(true,n);}
    return accepted;
}
static bool xfer(uint8_t port,uint8_t ep,xfer_result_t result,uint32_t n){
    if(ep==EP_OUT){
        if(result==XFER_RESULT_SUCCESS&&n)(void)emu_mbim_ntb_receive(rx,n,receive_ip,NULL);
        if(alt)return usbd_edpt_xfer(port,EP_OUT,rx,sizeof(rx),false);
    }
    if(ep==EP_IN&&tx_zlp){tx_zlp=false;return usbd_edpt_xfer(port,EP_IN,NULL,0,false);}
    return true;
}
bool emu_mbim_usb_can_xmit(size_t n){return alt&&n>=34&&n<=1514&&!usbd_edpt_busy(rh,EP_IN);}
void emu_mbim_usb_xmit(const uint8_t *ethernet,size_t n){
    if(n<34||ethernet[12]!=8||ethernet[13]!=0)return; /* No Ethernet/ARP in MBIM. */
    size_t length=emu_mbim_ntb_send(tx,sizeof(tx),ethernet+14,n-14,sequence++);
    if(length){
        tx_zlp=length%64==0&&length<sizeof(tx);
        if(usbd_edpt_xfer(rh,EP_IN,tx,(uint16_t)length,false)){atomic_fetch_add(&tx_packets,1);emu_mbim_wire_stats(false,n-14);}
        else tx_zlp=false;
    }
}
void emu_mbim_usb_diag(char *out,size_t cap){
    snprintf(out,cap,"\r\n+EMUMBIM: requests=%u,last_request=%u,last_cid=%u,last_status=%u,rx=%u,tx=%u,opens=%u,resets=%u,open_length=%u,mounted=%u,connected=%u,op=%u,info_length=%u,session=%u\r\nOK\r\n",
             atomic_load(&control_count),atomic_load(&last_request),atomic_load(&last_cid),atomic_load(&last_status),atomic_load(&rx_packets),atomic_load(&tx_packets),
             atomic_load(&opens),atomic_load(&resets),atomic_load(&last_open_length),(unsigned)tud_mounted(),(unsigned)tud_connected(),atomic_load(&last_operation),atomic_load(&last_info_length),atomic_load(&last_session));
}
void emu_mbim_usb_trace(unsigned back,char *out,size_t cap){
    unsigned cursor=atomic_load(&trace_cursor),v[TRACE_WORDS]={0};
    if(back>=TRACE_COUNT||cursor<=back){snprintf(out,cap,"\r\nERROR\r\n");return;}
    unsigned serial=cursor-back,index=(serial-1)%TRACE_COUNT;
    for(unsigned i=0;i<TRACE_WORDS;i++)v[i]=atomic_load(&trace_records[index][i]);
    if(v[0]!=serial||atomic_load(&trace_records[index][0])!=serial){snprintf(out,cap,"\r\nERROR\r\n");return;}
    snprintf(out,cap,"\r\n+EMUMBIMTRACE: seq=%u,event=%u,type=%u,tid=%u,service=%u,cid=%u,field=%u,length=%u,host_max=%u\r\nOK\r\n",
             v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]&65535,v[7]>>16);
}
bool emu_mbim_usb_trace_command(const char *command,char *out,size_t cap){
    static const char prefix[]="AT+EMUMBIMTRACE=";
    size_t n=sizeof(prefix)-1;if(strncmp(command,prefix,n))return false;
    char *end;unsigned long back=strtoul(command+n,&end,10);
    if(command[n]<'0'||command[n]>'9'||*end||back>63)snprintf(out,cap,"\r\nERROR\r\n");
    else emu_mbim_usb_trace((unsigned)back,out,cap);
    return true;
}
static const usbd_class_driver_t driver={.name="MBIM",.init=init,.deinit=deinit,.reset=reset,.open=open_driver,.control_xfer_cb=control_xfer,.xfer_cb=xfer};
const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *count_drivers){*count_drivers=1;return &driver;}
#endif
