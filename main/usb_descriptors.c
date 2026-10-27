#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "sdkconfig.h"
#include "usb_os.h"

#if CONFIG_EMU_MBIM
#define INTERFACES 2
#define TOTAL_LENGTH 95
#define USB_VERSION 0x0200
#define DEVICE_REVISION 0x0102
/* MBIM uses the SDK's Espressif/custom-driver identity, not the known
   config-less Huawei RNDIS product identity. This is a separate USB function. */
#define DEVICE_VID TINYUSB_ESPRESSIF_VID
#define DEVICE_PID 0x4000
#elif CONFIG_EMU_AT_USB
#define INTERFACES 4
#define TOTAL_LENGTH (TUD_CONFIG_DESC_LEN + TUD_RNDIS_DESC_LEN + TUD_CDC_DESC_LEN)
#else
#define INTERFACES 2
#define TOTAL_LENGTH (TUD_CONFIG_DESC_LEN + TUD_RNDIS_DESC_LEN)
#endif
#if !CONFIG_EMU_MBIM
#define USB_VERSION 0x0210
#define DEVICE_REVISION 0x0101
#define DEVICE_VID 0x12d1
#define DEVICE_PID 0x14db
#endif

static const tusb_desc_device_t device = {
    .bLength=sizeof(tusb_desc_device_t), .bDescriptorType=TUSB_DESC_DEVICE,
    .bcdUSB=USB_VERSION, .bDeviceClass=0xef, .bDeviceSubClass=2, .bDeviceProtocol=1,
    .bMaxPacketSize0=64, .idVendor=DEVICE_VID, .idProduct=DEVICE_PID,
    .bcdDevice=DEVICE_REVISION, .iManufacturer=1, .iProduct=2, .iSerialNumber=3, .bNumConfigurations=1
};

static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1,INTERFACES,0,TOTAL_LENGTH,0,500),
#if CONFIG_EMU_MBIM
    8,11,0,2,2,0x0e,0,0,
    9,4,0,0,1,2,0x0e,0,4,
    5,0x24,0,0x20,1,
    5,0x24,6,0,1,
    12,0x24,0x1b,0,1,0,4,0,0,0xdc,5,0x20,
    8,0x24,0x1c,0,1,1,0xdc,5,
    7,5,0x81,3,8,0,16,
    9,4,1,0,0,0x0a,0,2,0,
    9,4,1,1,2,0x0a,0,2,0,
    7,5,0x02,2,64,0,0,
    7,5,0x82,2,64,0,0,
#else
    TUD_RNDIS_DESCRIPTOR(0,4,0x81,8,0x02,0x82,64),
#if CONFIG_EMU_AT_USB
    /* Explicit V.250 AT protocol 01, rather than TinyUSB's generic CDC protocol 00. */
    8,11,2,2,2,2,1,0,
    9,4,2,0,1,2,2,1,5,
    5,0x24,0,0x10,1,
    5,0x24,1,0,3,
    4,0x24,2,2,
    5,0x24,6,2,3,
    7,5,0x83,3,8,0,16,
    9,4,3,0,2,0x0a,0,0,0,
    7,5,0x04,2,64,0,0,
    7,5,0x84,2,64,0,0,
#endif
#endif
};

static const char *strings[] = {
    (const char[]){0x09,0x04}, "SDWAN laboratory", "ESP32-S3 LTE emulator",
    "EMU-S3-0001",
#if CONFIG_EMU_MBIM
    "MBIM",
#else
    "RNDIS",
#endif
    "AT control"
};

#if !CONFIG_EMU_MBIM
const uint8_t *tud_descriptor_bos_cb(void) { return emu_usb_bos; }

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request) {
    if (stage != CONTROL_STAGE_SETUP) return true;
    size_t length;
    const uint8_t *response=emu_usb_os_response(request->bmRequestType,request->bRequest,
                                              request->wValue,request->wIndex,&length);
    if (!response) return false;
    return tud_control_xfer(rhport,request,(void *)(uintptr_t)response,(uint16_t)length);
}
#endif

void emu_usb_start(void) {
    _Static_assert(sizeof(configuration)==TOTAL_LENGTH,"USB configuration length");
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    cfg.descriptor.device=&device;
    cfg.descriptor.string=strings;
    cfg.descriptor.string_count=sizeof(strings)/sizeof(strings[0]);
    cfg.descriptor.full_speed_config=configuration;
    ESP_ERROR_CHECK(tinyusb_driver_install(&cfg));
}
