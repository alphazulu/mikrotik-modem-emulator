/* Bounded MBIM 1.0 Basic Connect for a Wi-Fi backed *emulated* LTE modem.
   Wire layouts follow USB MBIM / libmbim Basic Connect; no GLib dependency. */
#include "mbim.h"
#include <string.h>

const uint8_t emu_mbim_basic_uuid[16]={0xa2,0x89,0xcc,0x33,0xbc,0xbb,0x8b,0x4f,0xb6,0xb0,0x13,0x3e,0xc2,0xaa,0xe6,0xdf};
const uint8_t emu_mbim_internet_uuid[16]={0x7e,0x5e,0x2a,0x7e,0x4e,0x6f,0x72,0x72,0x73,0x6b,0x65,0x6e,0x7e,0x5e,0x2a,0x7e};
const uint8_t emu_mbim_ms_uuid[16]={0x3d,0x01,0xdc,0xc5,0xfe,0xf5,0x4d,0x05,0x0d,0x3a,0xbe,0xf7,0x05,0x8e,0x9a,0xaf};
const uint8_t emu_mbim_atds_uuid[16]={0x59,0x67,0xbd,0xcc,0x7f,0xd2,0x49,0xa2,0x9f,0x5c,0xb2,0xe7,0x0e,0x52,0x7d,0xb3};
static const uint32_t cids[]={1,2,3,4,5,6,8,9,10,11,12,13,15,16,19,20,21,22,23};
#define RADIO_NOTIFICATIONS ((1u<<3)|(1u<<9)|(1u<<10)|(1u<<11))
#define INITIAL_NOTIFICATIONS (RADIO_NOTIFICATIONS|(1u<<2)|(1u<<12)|(1u<<15))
static uint16_t u16(const uint8_t *p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p){return (uint32_t)u16(p)|(uint32_t)u16(p+2)<<16;}
static void p16(uint8_t *p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void p32(uint8_t *p,uint32_t n){p16(p,(uint16_t)n);p16(p+2,(uint16_t)(n>>16));}
static void p64(uint8_t *p,uint64_t n){p32(p,(uint32_t)n);p32(p+4,(uint32_t)(n>>32));}
static size_t align4(size_t n){return (n+3)&~(size_t)3;}
static void ascii(uint8_t *b,size_t pair,size_t *used,const char *str){
    size_t n=strlen(str);p32(b+pair,n?(uint32_t)*used:0);p32(b+pair+4,(uint32_t)n*2);
    for(size_t i=0;i<n;i++)p16(b+*used+2*i,(uint8_t)str[i]);
    *used=align4(*used+2*n);
}
/* SSIDs are opaque 32-byte values. Preserve valid BMP UTF-8 and replace
   invalid/non-BMP input; output always fits 64 UTF-16 bytes. */
static size_t ssid_text(uint8_t *b,size_t pair,size_t *used,const char *text){
    size_t start=*used,at=0;
    while(at<32&&text[at]){
        uint32_t ch=(uint8_t)text[at++];
        if(ch>=0x80){
            unsigned more=ch>=0xc2&&ch<=0xdf?1:ch>=0xe0&&ch<=0xef?2:0;
            uint32_t value=more?ch&((1u<<(6-more))-1):0;bool valid=more&&at+more<=32;
            for(unsigned j=0;j<more&&valid;j++){uint8_t c=(uint8_t)text[at+j];if((c&0xc0)!=0x80)valid=false;else value=(value<<6)|(c&63);}
            if(valid&&(value<0x80||(more==2&&value<0x800)||(value>=0xd800&&value<=0xdfff)))valid=false;
            if(valid){ch=value;at+=more;}else ch=0xfffd;
        }
        p16(b+*used,(uint16_t)ch);*used+=2;
    }
    p32(b+pair,*used==start?0:(uint32_t)start);p32(b+pair+4,(uint32_t)(*used-start));*used=align4(*used);return *used-start;
}
static size_t scan_info(uint8_t *v,const emu_mbim *s){
    size_t used=4+8*s->scan_count;p32(v,(uint32_t)s->scan_count);
    for(size_t i=0;i<s->scan_count;i++){
        uint8_t *provider=v+used;size_t size=32;char id[]="00101";id[4]=(char)('1'+i);
        ascii(provider,0,&size,id);p32(provider+8,8);ssid_text(provider,12,&size,s->scan_aps[i].ssid);
        int dbm=s->scan_aps[i].rssi;uint32_t csq=dbm<=-113?0:dbm>=-51?31:(uint32_t)((dbm+113)/2);
        p32(provider+20,1);p32(provider+24,csq);p32(provider+28,99);
        p32(v+4+8*i,(uint32_t)used);p32(v+8+8*i,(uint32_t)size);used+=size;
    }
    return used;
}
bool emu_mbim_scan_take(emu_mbim *s,uint32_t token){
    if(!token||!s->opened||!s->scan_pending||s->scan_started)return false;
    s->scan_token=token;s->scan_started=true;return true;
}
void emu_mbim_scan_complete(emu_mbim *s,uint32_t token,const emu_mbim_ap *aps,size_t count,bool ok){
    if(!s->scan_pending||!s->scan_started||s->scan_token!=token)return;
    s->scan_ok=ok&&count<=EMU_MBIM_SCAN_MAX&&(!count||aps);s->scan_count=s->scan_ok?count:0;
    if(s->scan_count)memcpy(s->scan_aps,aps,s->scan_count*sizeof(*aps));
    s->scan_ready=true;
}
static uint32_t ap_csq(const emu_mbim_ap *ap){
    int dbm=ap->rssi;return dbm<=-113?0:dbm>=-51?31:(uint32_t)((dbm+113)/2);
}
void emu_mbim_current_ap(emu_mbim *s,const emu_mbim_ap *ap){
    if(ap){
        if(!s->current_valid||ap_csq(&s->current_ap)!=ap_csq(ap))s->pending_notifications|=1u<<11;
        s->current_ap=*ap;s->current_valid=true;
    }else{if(s->current_valid)s->pending_notifications|=1u<<11;s->current_valid=false;}
}
/* Virtual B3 cells for test UI only. Wi-Fi dBm is used as a simulated RSRP;
   RSRQ=-10 and TA=0 are fixed synthetic values, not measured cellular RF. */
static size_t cell_record(uint8_t *v,const emu_mbim_ap *ap,unsigned fallback,bool serving){
    uint32_t id=1;int power=(int)fallback*2-113;
    if(ap){id=2166136261u;for(size_t j=0;j<6;j++)id=(id^ap->bssid[j])*16777619u;power=ap->rssi;}
    if(power<-140)power=-140;
    if(power>-44)power=-44;
    p32(v+8,id&0x0fffffffu);p32(v+12,ap&&ap->channel?1200+5u*ap->channel:1300);
    p32(v+16,id%504);p32(v+20,1);p32(v+24,(uint32_t)(int32_t)power);p32(v+28,(uint32_t)(int32_t)-10);
    size_t used=serving?36:32;if(serving)p32(v+32,0);ascii(v,0,&used,"00101");return used;
}
static size_t cell_info(uint8_t *v,const emu_mbim *s,const emu_modem *m,bool neighbors){
    size_t used=76;p32(v,32);p32(v+28,(uint32_t)used);
    size_t n=cell_record(v+used,s->current_valid?&s->current_ap:NULL,m->rssi,true);
    p32(v+32,(uint32_t)n);used+=n;
    if(neighbors){
        size_t start=used,count=0;used+=4;
        for(size_t i=0;i<s->scan_count&&count<s->scan_lte_limit;i++){
            const emu_mbim_ap *ap=&s->scan_aps[i];
            if(s->current_valid&&!memcmp(ap->bssid,s->current_ap.bssid,6))continue;
            used+=cell_record(v+used,ap,m->rssi,false);count++;
        }
        p32(v+60,(uint32_t)start);p32(v+64,(uint32_t)(used-start));p32(v+start,(uint32_t)count);
    }
    return used;
}
static bool utf16_field(const uint8_t *b,size_t n,size_t pair,size_t fixed,char *out,size_t cap){
    uint32_t off=u32(b+pair),len=u32(b+pair+4);size_t used=0;
    if(!len){if(off)return false;out[0]=0;return true;}
    if(off<fixed || off%4 || len%2 || off>n || len>n-off)return false;
    for(size_t i=0;i<len;i+=2){
        uint16_t ch=u16(b+off+i);if(!ch || (ch>=0xd800&&ch<=0xdfff))return false;
        size_t count=ch<0x80?1:ch<0x800?2:3;if(used+count>=cap)return false;
        if(count==1)out[used++]=(char)ch;
        else if(count==2){out[used++]=(char)(0xc0|(ch>>6));out[used++]=(char)(0x80|(ch&63));}
        else{out[used++]=(char)(0xe0|(ch>>12));out[used++]=(char)(0x80|((ch>>6)&63));out[used++]=(char)(0x80|(ch&63));}
    }
    out[used]=0;return true;
}
static size_t header(uint8_t *o,uint32_t type,uint32_t tid,size_t n){p32(o,type);p32(o+4,(uint32_t)n);p32(o+8,tid);return n;}
static size_t simple(uint8_t *o,uint32_t type,uint32_t tid,uint32_t status){p32(o+12,status);return header(o,type,tid,16);}
static size_t done(uint8_t *o,const uint8_t *uuid,uint32_t cid,uint32_t tid,uint32_t status,size_t n){
    p32(o+12,1);p32(o+16,0);memcpy(o+20,uuid,16);p32(o+36,cid);p32(o+40,status);p32(o+44,(uint32_t)n);
    return header(o,0x80000003u,tid,48+n);
}
static size_t indicate(uint8_t *o,uint32_t cid,size_t n){
    p32(o+12,1);p32(o+16,0);memcpy(o+20,emu_mbim_basic_uuid,16);p32(o+36,cid);p32(o+40,(uint32_t)n);
    return header(o,0x80000007u,0,44+n);
}
static size_t connect_info(uint8_t *b,const emu_modem *m,bool pending){
    p32(b,0);p32(b+4,emu_session_ready(m)?1:pending?2:3);p32(b+8,0);p32(b+12,1);
    memcpy(b+16,emu_mbim_internet_uuid,16);p32(b+32,0);return 36;
}
static size_t ip_info(uint8_t *b){
    p32(b,0);p32(b+4,15);p32(b+12,1);p32(b+16,60);p32(b+28,68);
    p32(b+36,1);p32(b+40,72);p32(b+52,1500);
    p32(b+60,24);memcpy(b+64,(uint8_t[]){192,0,2,2},4);memcpy(b+68,(uint8_t[]){192,0,2,1},4);
    memcpy(b+72,(uint8_t[]){1,1,1,1},4);return 76;
}
static size_t subscriber_info(uint8_t *v,const emu_modem *m){
    size_t used=28;p32(v,m->sim_ready?1:2);ascii(v,4,&used,"001010000000001");ascii(v,12,&used,"89001010000000000001");return used;
}
static size_t radio_info(uint8_t *v,const emu_modem *m){p32(v,1);p32(v+4,m->radio_on);return 8;}
static size_t register_info(uint8_t *v,const emu_modem *m){
    size_t used=48;p32(v+4,m->radio_on&&m->sim_ready?3:1);p32(v+8,1);p32(v+12,32);p32(v+16,1);
    ascii(v,20,&used,"00101");ascii(v,28,&used,"SDWAN WiFi LTE emulator");ascii(v,36,&used,"");return used;
}
static size_t packet_info(uint8_t *v,const emu_mbim *s,const emu_modem *m){
    p32(v+4,m->radio_on&&s->attached?2:4);p32(v+8,32);p64(v+12,12000000);p64(v+20,12000000);return 28;
}
static size_t signal_info(uint8_t *v,const emu_mbim *s,const emu_modem *m){
    p32(v,m->radio_on?(s->current_valid?ap_csq(&s->current_ap):m->rssi):99);p32(v+4,99);p32(v+8,s->signal_interval);p32(v+12,s->signal_threshold);p32(v+16,s->error_threshold);return 20;
}
void emu_mbim_init(emu_mbim *s){memset(s,0,sizeof(*s));s->attached=true;s->subscription=INITIAL_NOTIFICATIONS;s->signal_interval=5;s->signal_threshold=2;s->error_threshold=99;}

static uint32_t set_connect(emu_mbim *s,emu_modem *m,const uint8_t *b,size_t n,uint32_t tid,uint32_t now){
    if(n<60 || u32(b)!=0 || u32(b+4)>1 || u32(b+32) || u32(b+36)>2 ||
       (u32(b+40)!=0&&u32(b+40)!=1&&u32(b+40)!=3) || memcmp(b+44,emu_mbim_internet_uuid,16))return 21;
    if(s->activation.pending)return 1;
    if(u32(b+4)==0){m->requested=false;return 0;}
    if(!m->sim_ready)return 3;
    if(!m->radio_on)return 20;
    if(!s->attached)return 12;
    emu_profile p={0};p.auth=u32(b+36);
    bool valid=utf16_field(b,n,8,60,p.apn,sizeof(p.apn))&&utf16_field(b,n,16,60,p.user,sizeof(p.user))&&utf16_field(b,n,24,60,p.password,sizeof(p.password));
    if(valid){
        for(size_t a=8;a<=24;a+=8)for(size_t z=a+8;z<=24;z+=8){
            uint32_t x=u32(b+a),xl=u32(b+a+4),y=u32(b+z),yl=u32(b+z+4);
            if(xl&&yl&&x<y+yl&&y<x+xl)valid=false;
        }
    }
    bool applied=valid&&emu_set_profile(m,&p);if(applied)m->staged=p;emu_wipe(&p,sizeof(p));
    if(!valid)return 21;
    if(!applied)return 18;
    m->requested=true;
    if(!emu_session_ready(m)){s->pending_tid=tid;emu_activation_begin(&s->activation,m,now);}
    return 0;
}
static bool subscribe(emu_mbim *s,const uint8_t *b,size_t n){
    if(n<4)return false;
    uint32_t count=u32(b);if(count>4 || n<4+8*count)return false;uint32_t mask=0;
    for(uint32_t i=0;i<count;i++){
        uint32_t off=u32(b+4+8*i),len=u32(b+8+8*i);
        if(off<4+8*count || off%4 || off>n || len>n-off || len<20)return false;
        uint32_t c=u32(b+off+16);if(c>32 || len!=20+4*c || memcmp(b+off,emu_mbim_basic_uuid,16))return false;
        for(uint32_t j=0;j<c;j++){uint32_t cid=u32(b+off+20+4*j);if(cid>31)return false;mask|=1u<<cid;}
    }
    s->subscription=mask;s->pending_notifications|=mask&INITIAL_NOTIFICATIONS;return true;
}
size_t emu_mbim_request(emu_mbim *s,emu_modem *m,const uint8_t *q,size_t len,uint8_t *o,size_t cap,uint32_t now){
    if(!s||!m||!q||!o||cap<EMU_MBIM_CONTROL_MAX||len<12||len>EMU_MBIM_CONTROL_MAX||u32(q+4)!=len||q==o)return 0;
    memset(o,0,cap);uint32_t type=u32(q),tid=u32(q+8);
    if(type==1){if(len!=16)return 0;if(u32(q+12)<EMU_MBIM_CONTROL_MAX)return simple(o,0x80000004u,tid,8);emu_mbim_init(s);s->opened=true;s->last_radio=m->radio_on;s->last_sim=m->sim_ready;return simple(o,0x80000001u,tid,0);}
    if(type==2){if(len!=12)return 0;s->opened=false;s->activation.pending=false;s->scan_pending=false;m->requested=false;return simple(o,0x80000002u,tid,0);}
    if(type==4)return 0; /* HOST_ERROR has no response. */
    if(type!=3||len<48)return 0;
    if(!s->opened)return simple(o,0x80000004u,tid,5);
    if(u32(q+12)!=1||u32(q+16))return simple(o,0x80000004u,tid,2);
    uint32_t cid=u32(q+36),op=u32(q+40),n=u32(q+44),status=0;size_t used=0;
    const uint8_t *b=q+48;uint8_t *v=o+48;
    if(n!=len-48||op>1)return done(o,q+20,cid,tid,21,0);
    if(!memcmp(q+20,emu_mbim_atds_uuid,16)){
        if(cid!=1)return done(o,q+20,cid,tid,9,0);
        if(op||n)return done(o,q+20,cid,tid,21,0);
        unsigned csq=s->current_valid?ap_csq(&s->current_ap):m->rssi;
        int power=s->current_valid?s->current_ap.rssi:(int)m->rssi*2-113;
        if(power<-140)power=-140;
        if(power>-44)power=-44;
        bool valid=m->radio_on&&csq<=31;
        p32(v,valid?csq:99);p32(v+4,99);p32(v+8,255);p32(v+12,255);
        p32(v+16,valid?19:255);p32(v+20,valid?(uint32_t)(power+140):255);
        p32(v+24,valid?25:255); /* Fixed simulated SINR +20dB. */
        return done(o,q+20,cid,tid,0,28);
    }
    if(!memcmp(q+20,emu_mbim_ms_uuid,16)){
        if(cid!=11)return done(o,q+20,cid,tid,9,0);
        if(op||n!=20)return done(o,q+20,cid,tid,21,0);
        if(!m->radio_on)return done(o,q+20,cid,tid,20,0);
        if(!u32(b+12))return done(o,q+20,cid,tid,0,cell_info(v,s,m,false));
        if(s->scan_pending||s->activation.pending)return done(o,q+20,cid,tid,1,0);
        s->scan_kind=11;s->scan_lte_limit=u32(b+12)>EMU_MBIM_SCAN_MAX?EMU_MBIM_SCAN_MAX:u32(b+12);
        /* Automatic serving-cell polls may also request neighbors. Limit active
           Wi-Fi scanning to once/30s; the explicit network scanner stays fresh. */
        if(s->scan_cache_valid&&(uint32_t)(now-s->scan_completed_ms)<30000)return done(o,q+20,cid,tid,0,cell_info(v,s,m,true));
        s->scan_cache_valid=false;
        s->scan_pending=true;s->scan_started=false;s->scan_ready=false;s->scan_count=0;
        s->scan_tid=tid;s->scan_start=now;return 0;
    }
    if(memcmp(q+20,emu_mbim_basic_uuid,16))return done(o,q+20,cid,tid,9,0);
    switch(cid){
    case 1:
        if(op||n){status=21;break;}
        s->device_caps_seen=true;
        used=64;p32(v,2);p32(v+4,1);p32(v+12,2);p32(v+16,32);p32(v+28,1);
        ascii(v,32,&used,"");ascii(v,40,&used,"004999010640000");ascii(v,48,&used,"esp32-mbim-emu-dev2");ascii(v,56,&used,"ESP32-S3 WiFi LTE emulator");break;
    case 2:
        if(op||n){status=21;break;}
        used=subscriber_info(v,m);break;
    case 3:
        if(op){if(n!=4||u32(b)>1){status=21;break;}m->radio_on=u32(b)==1;s->pending_notifications|=RADIO_NOTIFICATIONS;if(!m->radio_on){m->wifi_ip=false;m->requested=false;}}
        else if(n){status=21;break;}
        /* Publish a coherent initial network snapshot after the first radio
           exchange. Hosts need not issue an explicit subscription before init. */
        if(!s->initial_sync){s->initial_sync=true;s->pending_notifications|=RADIO_NOTIFICATIONS;}
        used=radio_info(v,m);break;
    case 4:
        if(op||n){status=9;break;}
        used=12;p32(v,2);p32(v+4,0);p32(v+8,0xffffffffu);break;
    case 5:
        if(op||n){status=21;break;}used=160;break; /* All ten PIN types unsupported. */
    case 6:
        if(op||n){status=21;break;}
        used=32;ascii(v,0,&used,"00101");p32(v+8,25);ascii(v,12,&used,"SDWAN WiFi LTE emulator");p32(v+20,1);p32(v+24,m->rssi);p32(v+28,99);break;
    case 8:
        if(op||n!=4||u32(b)>1){status=21;break;}
        if(s->scan_pending||s->activation.pending){status=1;break;}
        if(!m->radio_on){status=20;break;}
        s->scan_kind=8;s->scan_cache_valid=false;s->scan_pending=true;s->scan_started=false;s->scan_ready=false;s->scan_count=0;
        s->scan_tid=tid;s->scan_start=now;return 0;
    case 9:
        if(op){if(n<16||u32(b+8)!=0){status=21;break;}s->pending_notifications|=1u<<9;}else if(n){status=21;break;}
        used=register_info(v,m);break;
    case 10:
        if(op){if(n!=4||u32(b)>1){status=21;break;}s->attached=u32(b)==0;s->pending_notifications|=1u<<10;if(!s->attached)m->requested=false;}else if(n){status=21;break;}
        used=packet_info(v,s,m);break;
    case 11:
        if(op){if(n!=12){status=21;break;}s->signal_interval=u32(b);s->signal_threshold=u32(b+4);s->error_threshold=u32(b+8);s->pending_notifications|=1u<<11;}else if(n){status=21;break;}
        used=signal_info(v,s,m);break;
    case 12:
        if(op){status=set_connect(s,m,b,n,tid,now);if(status)break;if(s->activation.pending)return 0;s->pending_notifications|=1u<<12;s->notify_ip=emu_session_ready(m);}
        else if(n!=4||u32(b)){status=21;break;}
        used=connect_info(v,m,s->activation.pending);break;
    case 15:
        /* Standard queries carry the full 60-byte structure; fields after
           SessionId are ignored. Keep the compact session-only query usable. */
        if(op||(n!=4&&n<60)||u32(b)){status=21;break;}
        if(!emu_session_ready(m)){status=16;break;}used=ip_info(v);break;
    case 13:
        if(op||n){status=28;break;}used=4;break; /* No provisioned/persistent contexts. */
    case 16:
        if(op||n){status=21;break;}
        p32(v,3);p32(v+8,32);p32(v+12,28+sizeof(cids));
        memcpy(v+32,emu_mbim_basic_uuid,16);p32(v+56,sizeof(cids)/sizeof(cids[0]));
        for(size_t i=0;i<sizeof(cids)/sizeof(cids[0]);i++)p32(v+60+4*i,cids[i]);
        used=60+sizeof(cids);p32(v+16,(uint32_t)used);p32(v+20,32);
        memcpy(v+used,emu_mbim_ms_uuid,16);p32(v+used+24,1);p32(v+used+28,11);used+=32;
        p32(v+24,(uint32_t)used);p32(v+28,32);memcpy(v+used,emu_mbim_atds_uuid,16);
        p32(v+used+24,1);p32(v+used+28,1);used+=32;break;
    case 19:
        if(!op||!subscribe(s,b,n)){status=21;break;}memcpy(v,b,n);used=n;break;
    case 20:
        if(op||n){status=21;break;}used=48;p64(v+8,s->rx_bytes);p64(v+16,s->rx_packets);p64(v+24,s->tx_bytes);p64(v+32,s->tx_packets);break;
    case 21:
        if((op&&(n!=4||u32(b)>1))||(!op&&n)){status=21;break;}used=4;break;
    case 22:
        if(op||n){status=9;break;}used=4;break;
    case 23:
        if(!op&&n==4&&!u32(b)){used=8;break;}status=32;break;
    default:status=9;break;
    }
    return done(o,q+20,cid,tid,status,status?0:used);
}
size_t emu_mbim_poll(emu_mbim *s,emu_modem *m,uint8_t *o,size_t cap,uint32_t now){
    if(!s||!m||!o||cap<EMU_MBIM_CONTROL_MAX||!s->opened)return 0;
    memset(o,0,cap);
    if(s->scan_pending){
        if(s->scan_ready||(uint32_t)(now-s->scan_start)>=15000){
            uint32_t status=s->scan_ready&&s->scan_ok?0:2;s->scan_pending=false;
            if(!status){s->scan_cache_valid=true;s->scan_completed_ms=now;}
            bool cells=s->scan_kind==11;
            size_t size=status?0:cells?cell_info(o+48,s,m,true):scan_info(o+48,s);
            return done(o,cells?emu_mbim_ms_uuid:emu_mbim_basic_uuid,cells?11:8,s->scan_tid,status,size);
        }
    }
    if(s->activation.pending){
        emu_activation_result result=emu_activation_poll(&s->activation,m,now);
        if(result==EMU_ACT_WAIT)return 0;
        s->connected=emu_session_ready(m);s->notify_ip=s->connected;
        s->pending_notifications|=1u<<12;
        return done(o,emu_mbim_basic_uuid,12,s->pending_tid,result==EMU_ACT_OK?0:2,connect_info(o+48,m,false));
    }
    if(s->last_radio!=m->radio_on||s->last_sim!=m->sim_ready){
        s->last_radio=m->radio_on;s->last_sim=m->sim_ready;s->pending_notifications|=RADIO_NOTIFICATIONS|(1u<<2);
    }
    /* Do not announce network objects until the host has enumerated device
       capabilities. Some hosts switch driver state after DeviceServices. */
    if(!s->device_caps_seen)return 0;
    uint32_t pending=s->pending_notifications&s->subscription;
    for(uint32_t cid=0;cid<32;cid++)if(pending&(1u<<cid)){
        s->pending_notifications&=~(1u<<cid);size_t n=0;
        switch(cid){
        case 2:n=subscriber_info(o+44,m);break;
        case 3:n=radio_info(o+44,m);break;
        case 9:n=register_info(o+44,m);break;
        case 10:n=packet_info(o+44,s,m);break;
        case 11:n=signal_info(o+44,s,m);break;
        case 12:s->connected=emu_session_ready(m);n=connect_info(o+44,m,false);break;
        case 15:if(emu_session_ready(m))n=ip_info(o+44);break;
        default:break;
        }
        if(n)return indicate(o,cid,n);
    }
    bool connected=emu_session_ready(m);
    if(s->connected!=connected){
        s->connected=connected;s->notify_ip=connected;
        if(s->subscription&(1u<<12))return indicate(o,12,connect_info(o+44,m,false));
    }
    if(s->notify_ip){s->notify_ip=false;if(s->subscription&(1u<<15))return indicate(o,15,ip_info(o+44));}
    return 0;
}

static bool ipv4(const uint8_t *p,size_t n){return n>=20&&n<=1500&&(p[0]>>4)==4&&(p[0]&15)>=5&&(size_t)(p[0]&15)*4<=n&&((size_t)p[2]<<8|p[3])==n;}
bool emu_mbim_ntb_receive(const uint8_t *b,size_t n,emu_mbim_ip_receiver receive,void *ctx){
    if(!b||!receive||n<28||n>EMU_MBIM_NTB_MAX||memcmp(b,"NCMH",4)||u16(b+4)!=12||u16(b+8)!=n)return false;
    size_t ndp=u16(b+10);if(ndp<12||ndp%4||ndp>n-16||memcmp(b+ndp,"IPS\0",4))return false;
    size_t table=u16(b+ndp+4);if(table<16||table%4||table>n-ndp||u16(b+ndp+6))return false;
    size_t offsets[4],sizes[4],count=0;bool end=false;
    for(size_t i=8;i<table;i+=4){
        size_t off=u16(b+ndp+i),len=u16(b+ndp+i+2);
        if(!off&&!len){end=true;break;}
        if(count==4||!len||off<12||off%4||off>n||len>n-off||!(off+len<=ndp||off>=ndp+table)||!ipv4(b+off,len))return false;
        for(size_t j=0;j<count;j++)if(off<offsets[j]+sizes[j]&&offsets[j]<off+len)return false;
        offsets[count]=off;sizes[count++]=len;
    }
    if(!end||!count)return false;
    for(size_t i=0;i<count;i++)if(!receive(b+offsets[i],sizes[i],ctx))return false;
    return true;
}
size_t emu_mbim_ntb_send(uint8_t *o,size_t cap,const uint8_t *ip,size_t n,uint16_t sequence){
    if(!o||!ip||!ipv4(ip,n)||cap<28+n)return 0;
    memset(o,0,28);memcpy(o,"NCMH",4);p16(o+4,12);p16(o+6,sequence);p16(o+8,(uint16_t)(28+n));p16(o+10,12);
    memcpy(o+12,"IPS\0",4);p16(o+16,16);p16(o+20,28);p16(o+22,(uint16_t)n);memcpy(o+28,ip,n);return 28+n;
}
