#ifndef HID_APP_H
#define HID_APP_H

#include <Arduino.h>
#include "Adafruit_TinyUSB.h"
#include "config.h"

extern hid_keyboard_report_t usb_keyboard_report;
extern hid_mouse_report_t usb_mouse_report;

// copy the latest keyboard report received by core1 into usb_keyboard_report (call from core0 only)
void hid_kbd_report_sync();
// copy the latest keyboard report received by core1 (any task, does not touch usb_keyboard_report)
void hid_kbd_report_get(hid_keyboard_report_t *report);

#ifdef __cplusplus
extern "C" {
#endif

void tuh_mount_cb(uint8_t daddr);
void tuh_umount_cb(uint8_t daddr);
void tuh_hid_mount_cb (uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len);
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance);
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
