#include "modem.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
static char response[EMU_AT_RESPONSE_MAX];
static emu_action action;
static void command(emu_modem *m, const char *s, const char *expected) {
    assert(emu_at(m, s, response, sizeof(response), &action));
    assert(strstr(response, expected));
}
static emu_profile profile(void) {
    emu_profile p = {.apn="wifi", .user="Demo AP", .password="test-only-key", .auth=1}; return p;
}
static void ready(emu_modem *m) {
    emu_init(m); emu_profile p = profile(); assert(emu_set_profile(m, &p));
    m->staged = p; m->requested = true; m->wifi_ip = true;
}
static void test_profile(void) {
    emu_profile p = profile(); emu_wifi w;
    assert(emu_profile_wifi(&p, &w)); assert(w.ssid_len == 7 && !memcmp(w.ssid,"Demo AP",7)); checks++;
    strcpy(p.apn,"wifi.JBSWY3DP"); p.user[0]=0;
    assert(emu_profile_wifi(&p,&w) && w.ssid_len==5 && !memcmp(w.ssid,"Hello",5)); checks++;
    strcpy(p.apn,"wifi.jbswy3dp"); assert(emu_profile_wifi(&p,&w)); checks++;
    const char *bad[] = {"internet","wifi.","wifi.A","wifi.AAA","wifi.AAAAAA","wifi.MZ","wifi.MY======","wifi.!!","wifi.AA"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){strcpy(p.apn,bad[i]);assert(!emu_profile_wifi(&p,&w));} checks++;
    strcpy(p.apn,"wifi.MY");strcpy(p.user,"ambiguous");assert(!emu_profile_wifi(&p,&w));checks++;
    p=profile();p.user[0]=0;assert(!emu_profile_wifi(&p,&w));p.user[0]=1;p.user[1]=0;assert(!emu_profile_wifi(&p,&w));checks++;
    p=profile();memset(p.user,'S',32);p.user[32]=0;assert(emu_profile_wifi(&p,&w)&&w.ssid_len==32);checks++;
    p=profile();p.auth=0;p.password[0]=0;assert(emu_profile_wifi(&p,&w));strcpy(p.password,"unexpected");assert(!emu_profile_wifi(&p,&w));checks++;
    p=profile();strcpy(p.password,"short");assert(!emu_profile_wifi(&p,&w));memset(p.password,'a',64);p.password[64]=0;assert(emu_profile_wifi(&p,&w));p.password[0]='g';assert(!emu_profile_wifi(&p,&w));checks++;
    p=profile();memset(p.apn,'x',sizeof(p.apn));assert(!emu_profile_wifi(&p,&w));p=profile();p.auth=3;assert(!emu_profile_wifi(&p,&w));assert(!emu_profile_wifi(NULL,&w));checks++;
}
static void test_commands(void) {
    emu_modem m;emu_init(&m);
    command(&m,"AT+CGDCONT=1,\"IP\",\"wifi\"","OK");
    command(&m,"AT+CGAUTH=1,1,\"Demo AP\",\"test-only-key\"","OK");
    assert(!m.configured); command(&m,"AT+CGACT=1,1","OK");assert(action==EMU_APPLY_WIFI&&m.configured&&m.requested&&!m.wifi_ip);checks++;
    uint32_t generation=m.generation;command(&m,"AT+CGACT=1,1","OK");assert(m.generation==generation);checks++;
    command(&m,"AT+CGAUTH?","+CGAUTH:");assert(!strstr(response,"test-only-key")&&!strstr(response,"Demo AP"));
    command(&m,"ATE1","OK");assert(!strstr(response,"ATE1"));checks++;
    m.wifi_ip=true;assert(emu_data_ready(&m));command(&m,"AT+CGACT?","1,1");
    command(&m,"AT+EMUGATE=1","OK");assert(m.blackhole&&!emu_data_ready(&m));command(&m,"AT+EMUGATE=0","OK");assert(emu_data_ready(&m));checks++;
    command(&m,"AT+CFUN=0","OK");assert(!m.radio_on&&!emu_data_ready(&m));command(&m,"AT+CFUN?","+CFUN: 0");
    command(&m,"AT+CGACT=1,1","CME ERROR");command(&m,"AT+CFUN=1","OK");assert(m.radio_on);checks++;
    command(&m,"AT&W","OK");assert(action==EMU_SAVE);command(&m,"AT+EMUFORGET","OK");assert(action==EMU_FORGET&&!m.configured&&!m.staged.password[0]);command(&m,"AT&W","ERROR");checks++;
    const char *bad[]={"AT+CGDCONT=2,\"IP\",\"wifi\"","AT+CGDCONT=1,\"IPV6\",\"wifi\"","AT+CGDCONT=1,\"IP\",\"wifi\",extra","AT+CGAUTH=1,9,\"x\",\"password\"","AT+CGAUTH=1,1,\"unterminated","AT+CGDCONT=1,\"IP\",\"wifi\";AT&W","AT+CGDCONT=1,\"IP\",\"bad\"\"quote\"","AT+UNKNOWN"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){emu_modem before=m;command(&m,bad[i],"ERROR");assert(!memcmp(&before,&m,sizeof(m)));}checks++;
    command(&m,"AT+CGAUTH=1,1,\"AP, with \"\"quote\"\"\",\"test-only-key\"","OK");assert(!strcmp(m.staged.user,"AP, with \"quote\""));checks++;
    emu_modem before=m;assert(!emu_at(&m,"AT+CFUN=0",response,8,&action));assert(!memcmp(&before,&m,sizeof(m)));checks++;
    command(&m,"AT+CPIN?","READY");m.sim_ready=false;command(&m,"AT+CPIN?","NOT INSERTED");command(&m,"AT+CEREG?","0,0");command(&m,"AT+CGACT=1,1","CME ERROR");checks++;
    m.sim_ready=true;command(&m,"AT+CEREG?","0,2");m.wifi_ip=true;command(&m,"AT+CREG?","0,1");command(&m,"AT+CGATT?","1");command(&m,"AT+CSQ","24,99");checks++;
    command(&m,"ATI","emulator");command(&m,"AT+CGMI","laboratory");command(&m,"AT+CGMR","dev1");command(&m,"AT+EMUSTATUS?","configured=0");checks++;
}
static void test_activation(void){
    emu_modem m;ready(&m);emu_activation a={0};m.wifi_ip=false;
    emu_activation_begin(&a,&m,100);assert(emu_activation_poll(&a,&m,101)==EMU_ACT_WAIT);
    m.wifi_ip=true;assert(emu_activation_poll(&a,&m,102)==EMU_ACT_OK&&!a.pending);checks++;
    m.wifi_ip=false;emu_activation_begin(&a,&m,100);assert(emu_activation_poll(&a,&m,30100)==EMU_ACT_ERROR&&!m.requested);m.wifi_ip=true;assert(!emu_data_ready(&m));checks++;
    ready(&m);m.wifi_ip=false;
    emu_activation_begin(&a,&m,0xfffffff0u);assert(emu_activation_poll(&a,&m,20)==EMU_ACT_WAIT);checks++;
    m.generation++;assert(emu_activation_poll(&a,&m,21)==EMU_ACT_ERROR);checks++;
    ready(&m);m.blackhole=true;assert(emu_session_ready(&m)&&!emu_data_ready(&m));command(&m,"AT+CGACT?","1,1");checks++;
    uint32_t g=m.generation;command(&m,"AT+EMUFORGET","OK");assert(m.generation>g);m.staged=profile();command(&m,"AT+CGACT=1,1","OK");assert(m.generation>g+1);checks++;
    ready(&m);m.generation=UINT32_MAX;emu_profile p=profile();strcpy(p.user,"Other AP");assert(!emu_set_profile(&m,&p));command(&m,"AT+EMUFORGET","OK");assert(m.generation==UINT32_MAX);checks++;
}
static void test_lines(void) {
    emu_line l={0};char out[EMU_AT_LINE_MAX+1];
    assert(!emu_line_byte(&l,'A',out,sizeof(out)));assert(!emu_line_byte(&l,'T',out,sizeof(out)));
    assert(emu_line_byte(&l,'\r',out,sizeof(out))&&!strcmp(out,"AT"));assert(!emu_line_byte(&l,'\n',out,sizeof(out)));checks++;
    for(unsigned i=0;i<EMU_AT_LINE_MAX+10;i++)assert(!emu_line_byte(&l,'x',out,sizeof(out)));
    assert(emu_line_byte(&l,'\r',out,sizeof(out))&&out[0]==0&&!l.used&&!l.overflow);checks++;
    emu_line_byte(&l,'A',out,sizeof(out));emu_line_byte(&l,0,out,sizeof(out));assert(emu_line_byte(&l,'\n',out,sizeof(out))&&!out[0]);checks++;
}
static void ipframe(uint8_t *f) {
    memset(f,0,64);f[12]=8;f[13]=0;f[14]=0x45;f[16]=0;f[17]=28;f[23]=17;
    f[26]=192;f[27]=0;f[28]=2;f[29]=2;f[30]=198;f[31]=51;f[32]=100;f[33]=1;
}
static void test_gate(void) {
    emu_modem m;ready(&m);uint8_t f[64];ipframe(f);assert(emu_frame_allowed(&m,f,sizeof(f),true));m.blackhole=true;assert(!emu_frame_allowed(&m,f,sizeof(f),true));checks++;
    f[30]=192;f[31]=0;f[32]=2;f[33]=1;assert(emu_frame_allowed(&m,f,sizeof(f),true));checks++;
    ipframe(f);memset(f+30,255,4);f[34]=0;f[35]=68;f[36]=0;f[37]=67;assert(emu_frame_allowed(&m,f,sizeof(f),true));f[20]=0x20;assert(!emu_frame_allowed(&m,f,sizeof(f),true));checks++;
    ipframe(f);f[12]=0x86;f[13]=0xdd;assert(!emu_frame_allowed(&m,f,sizeof(f),true));f[12]=0x81;f[13]=0;assert(!emu_frame_allowed(&m,f,sizeof(f),true));checks++;
    ipframe(f);f[14]=0x44;assert(!emu_frame_allowed(&m,f,sizeof(f),true));ipframe(f);f[17]=255;assert(!emu_frame_allowed(&m,f,sizeof(f),true));assert(!emu_frame_allowed(&m,f,13,true));checks++;
    ipframe(f);m.radio_on=false;m.blackhole=false;assert(!emu_frame_allowed(&m,f,sizeof(f),true));m.radio_on=true;m.sim_ready=false;assert(!emu_frame_allowed(&m,f,sizeof(f),true));checks++;
    ready(&m);m.wifi_ip=false;assert(!emu_frame_allowed(&m,f,sizeof(f),false));f[26]=192;f[27]=0;f[28]=2;f[29]=1;assert(emu_frame_allowed(&m,f,sizeof(f),false));checks++;
}
static void fuzz(void) {
    emu_modem m;emu_init(&m);uint32_t seed=7;char s[EMU_AT_LINE_MAX+2];uint8_t f[128];
    for(unsigned k=0;k<4000;k++){
        size_t n=k%sizeof(s);for(size_t j=0;j<n;j++){seed=1664525*seed+1013904223;s[j]=(char)(32+seed%95);}s[n]=0;
        assert(emu_at(&m,s,response,sizeof(response),&action));assert(!strcmp(response,"\r\nERROR\r\n"));
        for(size_t j=0;j<sizeof(f);j++){seed=1664525*seed+1013904223;f[j]=(uint8_t)(seed>>24);}
        (void)emu_frame_allowed(&m,f,k%sizeof(f),(k&1)!=0);
    }checks++;
}
int main(void){test_profile();test_commands();test_activation();test_lines();test_gate();fuzz();printf("PASS %u checks; 4000 deterministic malformed command/frame cases\n",checks);return 0;}
