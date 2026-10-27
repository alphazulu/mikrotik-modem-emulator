#ifndef EMU_MBIM_USB_H
#define EMU_MBIM_USB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* The following adapter functions execute with the modem mutex in main.c. */
size_t emu_mbim_command(const uint8_t *q,size_t n,uint8_t *o,size_t cap);
bool emu_mbim_command_pending(void);
size_t emu_mbim_events(uint8_t *o,size_t cap);
void emu_mbim_host_reset(void);
void emu_mbim_wire_stats(bool from_host,size_t n);
bool emu_mbim_host_ip(const uint8_t *ip,size_t n);
/* TinyUSB task context only, except diag() which reads atomic counters. */
void emu_mbim_usb_poll(void);
bool emu_mbim_usb_can_xmit(size_t ethernet_length);
void emu_mbim_usb_xmit(const uint8_t *ethernet,size_t length);
void emu_mbim_usb_diag(char *out,size_t cap);
/* Read metadata only; back=0 is newest. No MBIM information buffer is logged. */
void emu_mbim_usb_trace(unsigned back,char *out,size_t cap);
bool emu_mbim_usb_trace_command(const char *command,char *out,size_t cap);
#endif
