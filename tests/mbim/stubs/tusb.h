#ifndef TEST_TUSB_H
#define TEST_TUSB_H
#include <stdbool.h>
#include <stdint.h>
#define TU_ATTR_ALIGNED(n) __attribute__((aligned(n)))
enum {CONTROL_STAGE_SETUP,CONTROL_STAGE_DATA,CONTROL_STAGE_ACK};
enum {TUSB_REQ_GET_INTERFACE=10,TUSB_REQ_SET_INTERFACE=11};
typedef enum {XFER_RESULT_SUCCESS,XFER_RESULT_FAILED} xfer_result_t;
typedef struct __attribute__((packed)){uint8_t bmRequestType,bRequest;uint16_t wValue,wIndex,wLength;} tusb_control_request_t;
typedef struct __attribute__((packed)){uint8_t bLength,bDescriptorType,bInterfaceNumber,bAlternateSetting,bNumEndpoints,bInterfaceClass,bInterfaceSubClass,bInterfaceProtocol,iInterface;} tusb_desc_interface_t;
typedef struct __attribute__((packed)){uint8_t bLength,bDescriptorType,bEndpointAddress,bmAttributes;uint16_t wMaxPacketSize;uint8_t bInterval;} tusb_desc_endpoint_t;
bool tud_control_xfer(uint8_t,const tusb_control_request_t *,void *,uint16_t);
bool tud_control_status(uint8_t,const tusb_control_request_t *);
bool tud_mounted(void);
bool tud_connected(void);
#endif
