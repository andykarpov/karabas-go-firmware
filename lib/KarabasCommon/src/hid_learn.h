#pragma once

// Learning a drivers/*.ini mapping from raw HID reports (the OSD gamepad mapper).
// Pure C, no Arduino dependencies.
//
// First the pad rests while hid_learn_rest_add() collects reports: the last one and the range
// each byte jitters in. Then for each button hid_learn_detect() compares a report with the
// rest state and returns the source that changed:
//   - a hat nibble that left its centered value 8..15 -> type 2;
//   - one bit of a byte that does not jitter -> type 1 (set) or 4 (clear);
//   - otherwise the byte that moved most, at least HID_LEARN_AXIS_MIN beyond its rest range
//     -> type 3, a range from halfway between the rest and the pressed value to the end.
//     A byte that jitters around 0 (0xff..0x01) is read as signed.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hid_joy_map.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HID_LEARN_MAX_REPORT 64
#define HID_LEARN_AXIS_MIN 0x30

typedef struct {
  uint16_t len;      // 0: no report yet
  uint8_t report_id;
  uint8_t last[HID_LEARN_MAX_REPORT];
  uint8_t umin[HID_LEARN_MAX_REPORT], umax[HID_LEARN_MAX_REPORT];
  int8_t smin[HID_LEARN_MAX_REPORT], smax[HID_LEARN_MAX_REPORT];
} hid_learn_rest_t;

void hid_learn_rest_reset(hid_learn_rest_t *rest);
// add a report of the resting pad; reports with another id or length than the first are ignored
void hid_learn_rest_add(hid_learn_rest_t *rest, uint8_t report_id, const uint8_t *data, uint16_t len);

// the source that changed in a report against the rest state; false if nothing did
bool hid_learn_detect(const hid_learn_rest_t *rest, uint8_t report_id, const uint8_t *data, uint16_t len, hid_joy_rule_t *out);

// true if a report is back to the rest state (the buttons are released)
bool hid_learn_is_rest(const hid_learn_rest_t *rest, uint8_t report_id, const uint8_t *data, uint16_t len);

// "pos,type,value[,arg]" as in the ini; returns the length written (snprintf style)
int hid_learn_format_rule(const hid_joy_rule_t *r, char *buf, size_t size);

#ifdef __cplusplus
}
#endif
