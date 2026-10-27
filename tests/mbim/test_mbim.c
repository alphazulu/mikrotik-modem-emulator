#include "mbim.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t q[1024],o[1024],body[976];
static emu_mbim s;
static emu_modem m;
static uint32_t get(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t *p,uint32_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);}
static size_t cmd(uint32_t cid,uint32_t op,const uint8_t *b,size_t n){
    memset(q,0,sizeof(q));put(q,3);put(q+4,(uint32_t)(48+n));put(q+8,7);put(q+12,1);memcpy(q+20,emu_mbim_basic_uuid,16);put(q+36,cid);put(q+40,op);put(q+44,(uint32_t)n);if(n)memcpy(q+48,b,n);
    return emu_mbim_request(&s,&m,q,48+n,o,sizeof(o),100);
}
static void open_session(void){
    emu_mbim_init(&s);emu_init(&m);memset(q,0,sizeof(q));put(q,1);put(q+4,16);put(q+8,1);put(q+12,1024);
    assert(emu_mbim_request(&s,&m,q,16,o,sizeof(o),0)==16 && s.opened && get(o)==0x80000001);
    s.device_caps_seen=true; /* Most cases start after capability enumeration. */
}
static void dump(const char *name,size_t n){
    FILE *f=fopen(name,"wb");assert(f);assert(fwrite(o,1,n,f)==n);fclose(f);
}
static void startup_order(void){
    open_session();s.device_caps_seen=false;uint8_t enabled[4]={1};
    assert(cmd(3,1,enabled,4)==56&&s.pending_notifications);
    assert(emu_mbim_poll(&s,&m,o,sizeof(o),101)==0);
    assert(cmd(16,0,NULL,0)>48);assert(emu_mbim_poll(&s,&m,o,sizeof(o),101)==0);
    assert(cmd(1,0,NULL,0)>48&&s.device_caps_seen);
    assert(emu_mbim_poll(&s,&m,o,sizeof(o),101)>44);
}
static void queries(void){
    const unsigned cids[]={1,2,3,4,5,6,9,10,11,12,13,15,16,20,21,22,23};
    open_session();memset(body,0,sizeof(body));
    for(size_t i=0;i<sizeof(cids)/sizeof(cids[0]);i++){
        unsigned cid=cids[i];size_t n=cmd(cid,0,body,(cid==12||cid==15||cid==23)?4:0);
        assert(n>=48 && get(o)==0x80000003 && get(o+36)==cid);
        assert(get(o+40)==(cid==15?16u:0u));
        char name[40];snprintf(name,sizeof(name),"response-%u.bin",cid);dump(name,n);
        assert(cmd(cid,0,body,3)>=48 && get(o+40)!=0);
        assert(cmd(cid,1,body,3)>=48 && get(o+40)!=0);
    }
    assert(cmd(99,0,body,0)==48 && get(o+40)==9);
    assert(cmd(1,2,body,0)==48 && get(o+40)==21);
    assert(cmd(1,0,body,0)>48);q[20]^=1;assert(emu_mbim_request(&s,&m,q,48,o,sizeof(o),0)==48&&get(o+40)==9);
    q[20]^=1;put(q+44,2);assert(emu_mbim_request(&s,&m,q,48,o,sizeof(o),0)==48&&get(o+40)==21);
    put(q+12,2);assert(emu_mbim_request(&s,&m,q,48,o,sizeof(o),0)==16&&get(o+12)==2);
    s.opened=false;assert(emu_mbim_request(&s,&m,q,48,o,sizeof(o),0)==16&&get(o+12)==5);
}
static void control(void){
    open_session();memset(body,0,sizeof(body));
    assert(cmd(3,1,body,4)==56&&!m.radio_on);
    put(body,1);assert(cmd(3,1,body,4)==56&&m.radio_on);
    put(body,2);assert(cmd(3,1,body,4)==48&&get(o+40)==21);
    put(body,1);assert(cmd(10,1,body,4)==76&&!s.attached);
    put(body,0);assert(cmd(10,1,body,4)==76&&s.attached);
    put(body,9);assert(cmd(10,1,body,4)==48);
    put(body,5);put(body+4,3);put(body+8,99);assert(cmd(11,1,body,12)==68&&s.signal_threshold==3);
    put(body+8,0);assert(cmd(9,1,body,16)>48);
    put(body+8,1);assert(cmd(9,1,body,16)==48);
    put(body,0);assert(cmd(21,1,body,4)==52);assert(cmd(22,1,body,0)==48);assert(cmd(4,1,body,0)==48);
    m.sim_ready=false;assert(cmd(2,0,NULL,0)>48&&get(o+48)==2);m.sim_ready=true;
    put(body,1);put(body+4,12);put(body+8,28);memcpy(body+12,emu_mbim_basic_uuid,16);put(body+28,2);put(body+32,12);put(body+36,15);
    assert(cmd(19,1,body,40)==88 && (s.subscription&(1u<<12)) && (s.subscription&(1u<<15)));
    dump("subscribe-response.bin",88);
    for(size_t n=0;n<40;n++){assert(cmd(19,1,body,n)==48);}
    uint8_t copy[40];memcpy(copy,body,40);
    for(unsigned pos=0;pos<40;pos+=4){memcpy(body,copy,40);put(body+pos,0xffffffffu);assert(cmd(19,1,body,40)==48);}
    memset(body,0,sizeof(body));assert(cmd(19,1,body,4)==52&&s.subscription==0);
}
static size_t text(uint8_t *b,size_t pair,size_t used,const char *str){
    size_t n=strlen(str);if(n){put(b+pair,(uint32_t)used);put(b+pair+4,(uint32_t)n*2);for(size_t i=0;i<n;i++){b[used+2*i]=(uint8_t)str[i];b[used+2*i+1]=0;}}return (used+2*n+3)&~(size_t)3;
}
static size_t connect_body(void){
    memset(body,0,sizeof(body));put(body+4,1);put(body+36,1);put(body+40,1);memcpy(body+44,emu_mbim_internet_uuid,16);
    size_t n=text(body,8,60,"wifi"),n2=text(body,16,n,"DemoAP");return text(body,24,n2,"synthetic-test-key");
}
static void connect_session(void){
    open_session();size_t n=connect_body();assert(cmd(12,1,body,n)==0&&s.activation.pending&&m.configured&&!m.wifi_ip);
    assert(emu_mbim_poll(&s,&m,o,sizeof(o),101)==0);
    assert(cmd(12,1,body,n)==48&&get(o+40)==1);
    m.wifi_ip=true;assert(emu_mbim_poll(&s,&m,o,sizeof(o),102)==84&&get(o+40)==0);
    dump("connect-response.bin",84);s.subscription=(1u<<12)|(1u<<15);
    assert(emu_mbim_poll(&s,&m,o,sizeof(o),103)==80&&get(o+36)==12);dump("connect-indication.bin",80);
    assert(emu_mbim_poll(&s,&m,o,sizeof(o),103)==120&&get(o+36)==15);dump("ip-indication.bin",120);
    uint8_t z[4]={0};assert(cmd(15,0,z,4)==124);dump("ip-response.bin",124);
    uint8_t full_ip_query[60]={0};assert(cmd(15,0,full_ip_query,60)==124);dump("ip-response-full-query.bin",124);
    assert(cmd(12,1,body,n)==84);assert(cmd(12,0,z,4)==84);
    assert(emu_mbim_poll(&s,&m,o,sizeof(o),104)==80);assert(emu_mbim_poll(&s,&m,o,sizeof(o),104)==120);
    m.wifi_ip=false;assert(emu_mbim_poll(&s,&m,o,sizeof(o),104)==80&&get(o+36)==12);
    assert(cmd(12,1,body,n)==0);assert(emu_mbim_poll(&s,&m,o,sizeof(o),30100)==84&&get(o+40)==2&&!m.requested);
    assert(cmd(12,1,body,n)==0);m.generation++;assert(emu_mbim_poll(&s,&m,o,sizeof(o),200)==84&&get(o+40)==2);
    put(body+4,0);assert(cmd(12,1,body,n)==84&&!m.requested);
    put(body+4,1);m.sim_ready=false;assert(cmd(12,1,body,n)==48&&get(o+40)==3);m.sim_ready=true;
    m.radio_on=false;assert(cmd(12,1,body,n)==48&&get(o+40)==20);m.radio_on=true;
    s.attached=false;assert(cmd(12,1,body,n)==48&&get(o+40)==12);s.attached=true;
    uint8_t copy[976];memcpy(copy,body,n);
    for(size_t i=0;i<60;i+=4){memcpy(body,copy,n);put(body+i,0xffffffffu);assert(cmd(12,1,body,n)==48);}
    memcpy(body,copy,n);put(body+16,get(body+8));assert(cmd(12,1,body,n)==48&&get(o+40)==21);
    memcpy(body,copy,n);body[get(body+8)]=0;assert(cmd(12,1,body,n)==48);
    memcpy(body,copy,n);body[get(body+8)+1]=0xd8;assert(cmd(12,1,body,n)==48);
    memcpy(body,copy,n);put(body+12,2000);assert(cmd(12,1,body,n)==48);
    memset(body,0,60);put(body+4,1);put(body+40,1);memcpy(body+44,emu_mbim_internet_uuid,16);assert(cmd(12,1,body,60)==48&&get(o+40)==18);
    open_session();n=connect_body();size_t user=get(body+16);put(body+20,4);
    body[user]=0x2e;body[user+1]=4;body[user+2]=0x2d;body[user+3]=0x4e;
    assert(cmd(12,1,body,n)==0 && !strcmp(m.staged.user,"\xd0\xae\xe4\xb8\xad"));
}
static void scanner(void){
    open_session();uint8_t action[4]={0};
    assert(cmd(8,1,action,4)==48&&get(o+40)==21);
    action[0]=2;assert(cmd(8,0,action,4)==48&&get(o+40)==21);action[0]=0;
    m.radio_on=false;assert(cmd(8,0,action,4)==48&&get(o+40)==20);m.radio_on=true;
    assert(!emu_mbim_scan_take(&s,1));assert(cmd(8,0,action,4)==0&&s.scan_pending);
    assert(cmd(8,0,action,4)==48&&get(o+40)==1);assert(!emu_mbim_scan_take(&s,0));
    assert(emu_mbim_scan_take(&s,42));assert(!emu_mbim_scan_take(&s,43));
    emu_mbim_ap aps[EMU_MBIM_SCAN_MAX]={0};
    strcpy(aps[0].ssid,"VisibleTestAP");aps[0].rssi=-65;
    strcpy(aps[1].ssid,"\xd0\xa2\xd0\xb5\xd1\x81\xd1\x82");aps[1].rssi=-125;
    strcpy(aps[2].ssid,"\xff\xe0\x80\x80\xed\xa0\x80");aps[2].rssi=-30;
    memset(aps[3].ssid,'a',32);aps[3].rssi=-80;
    emu_mbim_scan_complete(&s,41,aps,8,true);assert(!s.scan_ready);
    s.pending_notifications=0;assert(!emu_mbim_poll(&s,&m,o,sizeof(o),101));
    emu_mbim_scan_complete(&s,42,aps,8,true);size_t n=emu_mbim_poll(&s,&m,o,sizeof(o),102);
    assert(n>48&&n<=1024&&get(o+36)==8&&get(o+40)==0&&get(o+48)==8);dump("scan-providers.bin",n);
    assert(cmd(8,0,action,4)==0);assert(emu_mbim_scan_take(&s,43));emu_mbim_scan_complete(&s,43,NULL,0,true);
    assert(emu_mbim_poll(&s,&m,o,sizeof(o),102)==52&&get(o+48)==0);dump("scan-empty.bin",52);
    assert(cmd(8,0,action,4)==0);assert(emu_mbim_scan_take(&s,44));emu_mbim_scan_complete(&s,44,NULL,1,true);
    assert(emu_mbim_poll(&s,&m,o,sizeof(o),102)==48&&get(o+40)==2);
    assert(cmd(8,0,action,4)==0);assert(emu_mbim_poll(&s,&m,o,sizeof(o),15100)==48&&get(o+40)==2);
    assert(cmd(8,0,action,4)==0);emu_mbim_init(&s);emu_mbim_scan_complete(&s,44,aps,8,true);assert(!s.scan_ready);
}
static uint32_t cell_now=100;
static size_t ms_cell(uint32_t count){
    uint8_t z[20]={0};put(z+12,count);(void)cmd(11,0,z,20);
    const uint8_t uuid[16]={0x3d,0x01,0xdc,0xc5,0xfe,0xf5,0x4d,0x05,0x0d,0x3a,0xbe,0xf7,0x05,0x8e,0x9a,0xaf};
    memcpy(q+20,uuid,16);return emu_mbim_request(&s,&m,q,68,o,sizeof(o),cell_now);
}
static void cell_snapshot(void){
    open_session();assert(ms_cell(0)==172&&get(o+48)==32);dump("cell-serving-bootstrap.bin",172);
    put(q+40,1);assert(emu_mbim_request(&s,&m,q,68,o,sizeof(o),100)==48&&get(o+40)==21);
    put(q+36,99);assert(emu_mbim_request(&s,&m,q,68,o,sizeof(o),100)==48&&get(o+40)==9);
    m.radio_on=false;assert(ms_cell(0)==48&&get(o+40)==20);m.radio_on=true;
    emu_mbim_ap current={.ssid="SyntheticTestAP",.rssi=-68,.channel=6,.bssid={2,1,2,3,4,5}};
    emu_mbim_current_ap(&s,&current);assert(s.current_valid);assert(ms_cell(0)==172);dump("cell-serving-wifi.bin",172);
    assert(cmd(11,0,NULL,0)==68&&get(o+48)==22);
    current.rssi=-30;emu_mbim_current_ap(&s,&current);assert(ms_cell(0)==172);assert(cmd(11,0,NULL,0)==68&&get(o+48)==31);
    current.rssi=-150;emu_mbim_current_ap(&s,&current);assert(ms_cell(0)==172);assert(cmd(11,0,NULL,0)==68&&get(o+48)==0);
    emu_mbim_current_ap(&s,NULL);emu_mbim_current_ap(&s,NULL);assert(!s.current_valid);
    emu_mbim_ap aps[8]={0};for(unsigned i=0;i<8;i++){aps[i]=current;aps[i].bssid[5]=(uint8_t)i;aps[i].rssi=(int16_t)(-70-2*i);aps[i].channel=(uint8_t)(1+i);}
    assert(ms_cell(255)==0&&s.scan_lte_limit==8);assert(ms_cell(1)==48&&get(o+40)==1);
    assert(emu_mbim_scan_take(&s,50));emu_mbim_scan_complete(&s,50,aps,8,true);
    size_t n=emu_mbim_poll(&s,&m,o,sizeof(o),101);assert(n==528&&get(o+36)==11&&get(o+40)==0);dump("cell-neighbors-eight.bin",n);
    emu_mbim_current_ap(&s,&aps[0]);cell_now=102;assert(ms_cell(2)==264&&!s.scan_pending); /* Fresh cache, no RF scan. */
    cell_now=30101;assert(ms_cell(2)==0);assert(emu_mbim_scan_take(&s,51));emu_mbim_scan_complete(&s,51,aps,8,true);
    n=emu_mbim_poll(&s,&m,o,sizeof(o),30102);assert(n==264);dump("cell-neighbors-two.bin",n);
    cell_now=60102;assert(ms_cell(8)==0);assert(emu_mbim_scan_take(&s,52));emu_mbim_scan_complete(&s,52,NULL,0,true);
    n=emu_mbim_poll(&s,&m,o,sizeof(o),60103);assert(n==176);dump("cell-neighbors-empty.bin",n);
    cell_now=90103;assert(ms_cell(1)==0);assert(emu_mbim_poll(&s,&m,o,sizeof(o),105103)==48&&get(o+40)==2);
}
static size_t atds_signal(void){
    (void)cmd(1,0,NULL,0);memcpy(q+20,emu_mbim_atds_uuid,16);return emu_mbim_request(&s,&m,q,48,o,sizeof(o),100);
}
static void extended_signal(void){
    open_session();assert(atds_signal()==76&&get(o+48)==24&&get(o+64)==19&&get(o+68)==75&&get(o+72)==25);dump("atds-signal.bin",76);
    put(q+36,9);assert(emu_mbim_request(&s,&m,q,48,o,sizeof(o),100)==48&&get(o+40)==9);
    put(q+36,1);put(q+40,1);assert(emu_mbim_request(&s,&m,q,48,o,sizeof(o),100)==48&&get(o+40)==21);
    m.radio_on=false;assert(atds_signal()==76&&get(o+48)==99&&get(o+72)==255);m.radio_on=true;
    m.rssi=99;assert(atds_signal()==76&&get(o+68)==255);
    emu_mbim_ap ap={.rssi=-40};emu_mbim_current_ap(&s,&ap);assert(atds_signal()==76&&get(o+68)==96);
    ap.rssi=-150;emu_mbim_current_ap(&s,&ap);assert(atds_signal()==76&&get(o+68)==0);
}
static unsigned received;
static void notifications(void){
    open_session();emu_profile p={.apn="wifi",.user="DemoAP",.password="synthetic-test-key",.auth=1};
    assert(emu_set_profile(&m,&p));m.requested=m.wifi_ip=true;
    const uint32_t ids[]={2,3,9,10,11,12,15};memset(body,0,sizeof(body));put(body,1);put(body+4,12);put(body+8,48);
    memcpy(body+12,emu_mbim_basic_uuid,16);put(body+28,7);
    for(size_t i=0;i<7;i++)put(body+32+4*i,ids[i]);
    assert(cmd(19,1,body,60)==108);
    for(size_t i=0;i<7;i++){
        size_t n=emu_mbim_poll(&s,&m,o,sizeof(o),100);assert(n>44&&get(o)==0x80000007&&get(o+36)==ids[i]);
        char name[40];snprintf(name,sizeof(name),"notify-%u.bin",ids[i]);dump(name,n);
    }
    assert(!emu_mbim_poll(&s,&m,o,sizeof(o),101));
    m.radio_on=false;
    for(unsigned i=0;i<8;i++)(void)emu_mbim_poll(&s,&m,o,sizeof(o),102);
    m.wifi_ip=false;m.sim_ready=false;(void)emu_mbim_poll(&s,&m,o,sizeof(o),103);
    open_session();s.pending_notifications=s.subscription=1u<<15;assert(!emu_mbim_poll(&s,&m,o,sizeof(o),100));
}
static bool receive(const uint8_t *ip,size_t n,void *ctx){assert(ctx==&received&&n==20&&ip[0]==0x45);received++;return true;}
static bool reject(const uint8_t *ip,size_t n,void *ctx){(void)ip;(void)n;(void)ctx;return false;}
static void ntb(void){
    uint8_t ip[20]={0x45,0,0,20},buf[2048];size_t n=emu_mbim_ntb_send(buf,sizeof(buf),ip,20,65535);assert(n==48);
    assert(emu_mbim_ntb_receive(buf,n,receive,&received)&&received==1);
    assert(!emu_mbim_ntb_receive(buf,n,reject,NULL));
    for(size_t i=0;i<n;i++){uint8_t copy[2048];memcpy(copy,buf,n);copy[i]^=255;(void)emu_mbim_ntb_receive(copy,n,receive,&received);}
    assert(!emu_mbim_ntb_send(buf,47,ip,20,0));assert(!emu_mbim_ntb_send(NULL,2048,ip,20,0));assert(!emu_mbim_ntb_send(buf,2048,NULL,20,0));
    assert(!emu_mbim_ntb_receive(NULL,48,receive,&received));assert(!emu_mbim_ntb_receive(buf,48,NULL,NULL));
    assert(!emu_mbim_ntb_receive(buf,2049,receive,&received));
}
static void malformed(void){
    open_session();uint8_t b[1024];uint32_t seed=7;
    for(unsigned i=0;i<10000;i++){
        for(size_t j=0;j<sizeof(b);j++){seed=1664525*seed+1013904223;b[j]=(uint8_t)(seed>>24);}
        size_t n=i%1025;if(n>=12)put(b+4,(uint32_t)n);
        (void)emu_mbim_request(&s,&m,b,n,o,sizeof(o),i);
        (void)emu_mbim_ntb_receive(b,n,reject,NULL);
    }
    assert(!emu_mbim_request(NULL,&m,q,12,o,1024,0));assert(!emu_mbim_request(&s,NULL,q,12,o,1024,0));
    assert(!emu_mbim_request(&s,&m,NULL,12,o,1024,0));assert(!emu_mbim_request(&s,&m,q,12,NULL,1024,0));
    assert(!emu_mbim_request(&s,&m,q,12,o,1000,0));assert(!emu_mbim_request(&s,&m,q,12,q,1024,0));
    assert(!emu_mbim_poll(NULL,&m,o,1024,0));assert(!emu_mbim_poll(&s,NULL,o,1024,0));assert(!emu_mbim_poll(&s,&m,NULL,1024,0));assert(!emu_mbim_poll(&s,&m,o,1000,0));
    memset(q,0,16);put(q,1);put(q+4,16);put(q+12,64);assert(emu_mbim_request(&s,&m,q,16,o,1024,0)==16&&get(o+12)==8);
    put(q,2);put(q+4,12);assert(emu_mbim_request(&s,&m,q,12,o,1024,0)==16&&!s.opened);
    assert(!emu_mbim_poll(&s,&m,o,1024,0));put(q,4);assert(!emu_mbim_request(&s,&m,q,12,o,1024,0));
}
int main(void){startup_order();queries();scanner();cell_snapshot();extended_signal();control();connect_session();notifications();ntb();malformed();puts("PASS MBIM BasicConnect/NTB16 cases + 10000 malformed messages");return 0;}
