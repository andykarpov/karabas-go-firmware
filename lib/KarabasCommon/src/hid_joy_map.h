#pragma once

// USB HID joystick driver mapping (drivers/*.ini). Pure C++, no Arduino dependencies.
//
// A button is mapped by one or more sources, separated by '|'. A source is
// "pos,type,value[,arg]", pos being the byte in the report (after the report id byte):
//   0  byte == value
//   1  bit value (0..7) is set
//   2  hat switch: the low nibble of the byte, shifted right by arg (default 0), is value
//      (0..7) or a diagonal next to it; 8..15 is centered
//   3  value <= byte <= arg (an axis range)
//   4  bit value (0..7) is clear
// Example: up = 1,3,0,63|5,2,0 (stick up or hat up). Types 0 and 1 are the original format.

#include <stdint.h>

#define HID_JOY_BUTTONS 12
#define HID_JOY_MAX_RULES 32

enum hid_joy_type_e : uint8_t {
  HID_JOY_VALUE = 0,
  HID_JOY_BIT = 1,
  HID_JOY_HAT = 2,
  HID_JOY_RANGE = 3,
  HID_JOY_BIT_CLEAR = 4,
};

typedef struct
{
  uint16_t btn; // sega button bit
  uint8_t pos;  // byte position in the hid report
  uint8_t type; // hid_joy_type_e
  uint8_t val;
  uint8_t arg;
} hid_joy_rule_t;

typedef struct
{
  uint16_t vid;
  uint16_t pid;
  int16_t report_id; // -1: any report
  uint8_t count;
  hid_joy_rule_t rules[HID_JOY_MAX_RULES];
} hid_joy_config_t;

// Parse a button mapping value and append its rules for sega button btn. Missing fields
// default to 0. Returns false on an empty or malformed value, or when the rules don't fit;
// nothing is appended then.
bool hid_joy_rules_parse(const char *value, uint16_t btn, hid_joy_config_t *cfg);

// true if the driver handles reports with this id (0: the device has no report ids)
bool hid_joy_accepts_report(const hid_joy_config_t *cfg, uint8_t report_id);

// Map a raw HID report to sega button bits, starting from base (e.g. SC_CTL_ON).
// Rules whose byte position is outside the report are ignored.
uint16_t hid_joy_map_report(const hid_joy_config_t *cfg, const uint8_t *report, uint16_t len, uint16_t base);
