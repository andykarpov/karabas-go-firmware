#pragma once
#include <cstdint>
#define CFG_TUH_DEVICE_MAX 4
#define CFG_TUSB_MEM_ALIGN alignas(4)
#define TU_ATTR_WEAK __attribute__((weak))
enum xfer_result_t { XFER_RESULT_SUCCESS, XFER_RESULT_FAILED, XFER_RESULT_STALLED };
enum { TUSB_DESC_INTERFACE = 4, TUSB_DESC_ENDPOINT = 5, TUSB_DESC_INTERFACE_ASSOCIATION = 11, TUSB_XFER_BULK = 2 };
struct __attribute__((packed)) tusb_desc_interface_t {
  uint8_t bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting, bNumEndpoints;
  uint8_t bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol, iInterface;
};
struct __attribute__((packed)) tusb_desc_endpoint_t {
  uint8_t bLength, bDescriptorType, bEndpointAddress;
  struct { uint8_t xfer:2, reserved:6; } bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t bInterval;
};
struct tusb_control_request_t {
  uint8_t bmRequestType, bRequest;
  uint16_t wValue, wIndex, wLength;
};
struct tuh_xfer_t {
  uint8_t daddr{}, ep_addr{};
  xfer_result_t result{};
  uint32_t actual_len{};
  const tusb_control_request_t *setup{};
  uint8_t *buffer{};
  void (*complete_cb)(tuh_xfer_t *){};
};
inline uint16_t tu_edpt_packet_size(const tusb_desc_endpoint_t *ep) { return ep->wMaxPacketSize; }
inline uint16_t tu_htole16(uint16_t v) { return v; }
bool tuh_edpt_open(uint8_t, const tusb_desc_endpoint_t *);
bool tuh_edpt_close(uint8_t, uint8_t);
bool tuh_vid_pid_get(uint8_t, uint16_t *, uint16_t *);
bool tuh_mounted(uint8_t);
bool tuh_control_xfer(tuh_xfer_t *);
