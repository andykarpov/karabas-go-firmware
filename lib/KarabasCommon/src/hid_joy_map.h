#pragma once

// USB HID joystick driver mapping (drivers/*.ini). Pure C++, no Arduino dependencies.

#include <stdint.h>

#define HID_JOY_BUTTONS 12

typedef struct
{
  uint16_t btn; // button id (sega)
  uint8_t pos; // byte position in the hid report
  bool isBit; // is value is bit num or absolute value
  bool isAvail; // is button configured
  uint8_t val; // value (absolute or bit position)
} hid_joy_button_config_t;

typedef struct
{
  uint16_t vid;
  uint16_t pid;
  hid_joy_button_config_t buttons[HID_JOY_BUTTONS];
} hid_joy_config_t;

// Parse a button mapping value "pos,type,value" (type: 0 = absolute value, 1 = bit number).
// Missing fields default to 0. Returns false on an empty or malformed value.
bool hid_joy_button_parse(const char *value, hid_joy_button_config_t *out);

// Map a raw HID report to sega button bits, starting from base (e.g. SC_CTL_ON).
// Buttons whose byte position is outside the report are ignored.
uint16_t hid_joy_map_report(const hid_joy_config_t *cfg, const uint8_t *report, uint16_t len, uint16_t base);
