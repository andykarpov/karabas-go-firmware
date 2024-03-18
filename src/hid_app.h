#ifndef HID_APP_H
#define HID_APP_H

#include <Arduino.h>
#include "Adafruit_TinyUSB.h"

#define LANGUAGE_ID 0x0409 // Language ID: English
#define MAX_REPORT  4 // Max USB reports per instance

extern hid_keyboard_report_t usb_keyboard_report;
extern hid_mouse_report_t usb_mouse_report;

// Each HID instance can has multiple reports
static struct
{
  uint8_t report_count;
  tuh_hid_report_info_t report_info[MAX_REPORT];
  uint16_t vid;
  uint16_t pid;
} hid_info[CFG_TUH_HID];

// joystick report struct
typedef struct
{
    uint8_t axis[3];
    uint8_t buttons[3];
} hid_joystick_report_t;

typedef struct
{
  uint8_t id;
  uint8_t buttons; /**< buttons mask for currently pressed buttons in the mouse. */
  int16_t  x;       /**< Current delta x movement of the mouse. */
  int16_t  y;       /**< Current delta y movement on the mouse. */
  int8_t  wheel;   /**< Current delta wheel movement on the mouse. */
  int8_t  pan;     // using AC Pan
} hid_mouse_report_ext_t;

typedef struct
{
  uint8_t id;
  uint8_t mod;
  uint8_t bits[16];
} hid_keyboard_report_ext_t;

#ifdef __cplusplus
extern "C" {
#endif

void tuh_hid_mount_cb (uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len);
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance);
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);

#ifdef __cplusplus
}
#endif

static void process_kbd_report(uint8_t dev_addr, uint8_t instance, hid_keyboard_report_t const* report, uint16_t len);
static void process_kbd_report_ext(uint8_t dev_addr, uint8_t instance, hid_keyboard_report_ext_t const* report, uint16_t len);
static void process_mouse_report(uint8_t dev_addr, uint8_t instance, hid_mouse_report_t const* report, uint16_t len);
static void process_mouse_report_ext(uint8_t dev_addr, uint8_t instance, hid_mouse_report_ext_t const* report, uint16_t len);
static void process_joystick_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);
static void process_gamepad_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);

#endif
