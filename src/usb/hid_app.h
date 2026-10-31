#ifndef HID_APP_H
#define HID_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tusb.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

extern hid_keyboard_report_t usb_keyboard_report;
extern hid_mouse_report_t usb_mouse_report;

// copy the latest keyboard report received by core1 into usb_keyboard_report (call from core0 only)
void hid_kbd_report_sync();
// copy the latest keyboard report received by core1 (any task, does not touch usb_keyboard_report)
void hid_kbd_report_get(hid_keyboard_report_t *report);

// AT+KARABAS=key / type (APP): a keyboard report as if a USB keyboard sent it (to the core, the
// OSD and the hotkeys); core1 takes them in hid_kbd_inject_tick(), a press for 100 ms, a release 40. false: the queue is
// full. hid_kbd_inject_free(): the reports that still fit.
bool hid_kbd_inject(const hid_keyboard_report_t *report);
int hid_kbd_inject_free();
void hid_kbd_inject_tick(); // core1, usb_host_loop

// The mouse moves for the graphic menus (APP): the sums since the last take, the buttons held and
// the ones pressed since then (a short click between two takes is not lost).
typedef struct {
  int32_t dx, dy, wheel;
  uint8_t buttons;
  uint8_t pressed;
  bool changed;
} hid_mouse_move_t;

bool hid_mouse_take(hid_mouse_move_t *m); // false: nothing since the last take

// Raw reports of one pad, for the OSD gamepad mapper (APP). After start, the pad whose report
// changes first is followed; get returns its latest report (seq counts them).
typedef struct {
  uint16_t vid, pid;
  uint8_t report_id; // 0: the pad has no report ids
  uint16_t len;
  uint8_t data[64];  // the report without the report id byte
  uint32_t seq;
} hid_capture_t;

// The pads on the two USB joystick ports and what maps them (APP)
enum { USB_PAD_NONE, USB_PAD_DRIVER, USB_PAD_DEFAULT, USB_PAD_XINPUT };
typedef struct {
  uint8_t source;   // USB_PAD_*; NONE: a free port
  uint16_t vid, pid;
  const char *file; // the driver ini (USB_PAD_DRIVER)
} usb_pad_t;
void usb_pads_get(usb_pad_t pads[2]);

void hid_capture_start();
void hid_capture_stop();
bool hid_capture_get(hid_capture_t *out); // false until the pad is found

void tuh_mount_cb(uint8_t daddr);
void tuh_umount_cb(uint8_t daddr);
void tuh_hid_mount_cb (uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len);
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance);
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
