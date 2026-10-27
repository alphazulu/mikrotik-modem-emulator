#include "usb_os.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
int main(void) {
    const uint8_t uuid[] = {0xdf,0x60,0xdd,0xd8,0x89,0x45,0xc7,0x4c,0x9c,0xd2,0x65,0x9d,0x9e,0x64,0x8a,0x9f};
    assert(emu_usb_bos[0]==5 && emu_usb_bos[1]==15 && le16(emu_usb_bos+2)==33 && emu_usb_bos[4]==1);
    assert(emu_usb_bos[5]==28 && emu_usb_bos[6]==16 && emu_usb_bos[7]==5 && emu_usb_bos[8]==0);
    assert(!memcmp(emu_usb_bos+9,uuid,sizeof(uuid)));
    assert(!memcmp(emu_usb_bos+25,"\x00\x00\x03\x06",4));
    assert(le16(emu_usb_bos+29)==46 && emu_usb_bos[31]==EMU_MS_VENDOR_CODE && emu_usb_bos[32]==0);
    size_t n=999;
    const uint8_t *d=emu_usb_os_response(0xc0,EMU_MS_VENDOR_CODE,0,7,&n);
    assert(d && n==46 && d==emu_ms_os_set);
    assert(le16(d)==10 && le16(d+2)==0 && le16(d+8)==n && !memcmp(d+4,"\x00\x00\x03\x06",4));
    assert(le16(d+10)==8 && le16(d+12)==1 && d[14]==1 && d[15]==0 && le16(d+16)==n-10);
    assert(le16(d+18)==8 && le16(d+20)==2 && d[22]==0 && d[23]==0 && le16(d+24)==n-18);
    assert(le16(d+26)==20 && le16(d+28)==3 && !memcmp(d+30,"RNDIS\0\0\0",8));
    assert(!memcmp(d+38,"5162001\0",8));
    unsigned rejected=0;
    for(unsigned type=0;type<256;type++) {
        if(type==0xc0)continue;
        n=999;assert(!emu_usb_os_response((uint8_t)type,EMU_MS_VENDOR_CODE,0,7,&n) && n==0);rejected++;
    }
    for(unsigned req=0;req<256;req++) {
        if(req==EMU_MS_VENDOR_CODE)continue;
        n=999;assert(!emu_usb_os_response(0xc0,(uint8_t)req,0,7,&n) && n==0);rejected++;
    }
    n=999;assert(!emu_usb_os_response(0xc0,EMU_MS_VENDOR_CODE,1,7,&n) && n==0);
    n=999;assert(!emu_usb_os_response(0xc0,EMU_MS_VENDOR_CODE,0,8,&n) && n==0);
    assert(!emu_usb_os_response(0xc0,EMU_MS_VENDOR_CODE,0,7,NULL));
    puts("PASS USB BOS/MS OS 2.0 contract; valid request + 513 invalid requests");
    assert(rejected==510);
    return 0;
}
