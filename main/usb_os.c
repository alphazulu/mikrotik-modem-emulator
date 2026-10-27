#include "usb_os.h"

/* USB BOS: one Microsoft platform capability, Windows >= 8.1, descriptor set 46B. */
const uint8_t emu_usb_bos[EMU_USB_BOS_SIZE] = {
    5, 15, EMU_USB_BOS_SIZE, 0, 1,
    28, 16, 5, 0,
    0xdf, 0x60, 0xdd, 0xd8, 0x89, 0x45, 0xc7, 0x4c,
    0x9c, 0xd2, 0x65, 0x9d, 0x9e, 0x64, 0x8a, 0x9f,
    0, 0, 3, 6, EMU_MS_OS_SET_SIZE, 0, EMU_MS_VENDOR_CODE, 0
};

/* Configuration VALUE 1, first function interface 0 (RNDIS only, never AT ACM). */
const uint8_t emu_ms_os_set[EMU_MS_OS_SET_SIZE] = {
    10, 0, 0, 0, 0, 0, 3, 6, EMU_MS_OS_SET_SIZE, 0,
    8, 0, 1, 0, 1, 0, EMU_MS_OS_SET_SIZE - 10, 0,
    8, 0, 2, 0, 0, 0, EMU_MS_OS_SET_SIZE - 18, 0,
    20, 0, 3, 0,
    'R', 'N', 'D', 'I', 'S', 0, 0, 0,
    '5', '1', '6', '2', '0', '0', '1', 0
};

const uint8_t *emu_usb_os_response(uint8_t request_type, uint8_t request,
                                 uint16_t value, uint16_t index, size_t *length) {
    if (!length) return NULL;
    *length = 0;
    /* IN | VENDOR | DEVICE; unknown/OUT/interface requests must stall. */
    if (request_type != 0xc0 || request != EMU_MS_VENDOR_CODE || value || index != 7) return NULL;
    *length = sizeof(emu_ms_os_set);
    return emu_ms_os_set;
}
