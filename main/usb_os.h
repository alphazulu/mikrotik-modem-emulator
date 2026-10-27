#ifndef EMU_USB_OS_H
#define EMU_USB_OS_H
#include <stddef.h>
#include <stdint.h>

/* Microsoft OS 2.0 affects Windows driver selection only; RNDIS stays 02/02/ff. */
#define EMU_MS_VENDOR_CODE 0x20
#define EMU_MS_OS_SET_SIZE 46
#define EMU_USB_BOS_SIZE 33
extern const uint8_t emu_usb_bos[EMU_USB_BOS_SIZE];
extern const uint8_t emu_ms_os_set[EMU_MS_OS_SET_SIZE];
const uint8_t *emu_usb_os_response(uint8_t request_type, uint8_t request,
                                 uint16_t value, uint16_t index, size_t *length);
#endif
