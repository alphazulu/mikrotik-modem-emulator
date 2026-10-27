#include "tusb.h"
typedef struct {
 const char *name;
 void (*init)(void);
 bool (*deinit)(void);
 void (*reset)(uint8_t);
 uint16_t (*open)(uint8_t,const tusb_desc_interface_t *,uint16_t);
 bool (*control_xfer_cb)(uint8_t,uint8_t,const tusb_control_request_t *);
 bool (*xfer_cb)(uint8_t,uint8_t,xfer_result_t,uint32_t);
} usbd_class_driver_t;
const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *);
bool usbd_edpt_open(uint8_t,const tusb_desc_endpoint_t *);
void usbd_edpt_close(uint8_t,uint8_t);
bool usbd_edpt_xfer(uint8_t,uint8_t,uint8_t *,uint16_t,bool);
bool usbd_edpt_busy(uint8_t,uint8_t);
