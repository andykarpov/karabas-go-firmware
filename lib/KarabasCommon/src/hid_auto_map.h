#pragma once

// Default mapping of a USB HID joystick / gamepad to Sega MD2 buttons, built from its HID
// report descriptor, for pads without a drivers/*.ini. Pure C++, no Arduino dependencies.
//
// Only fields inside a top-level application collection Joystick, Gamepad or Multi-axis
// controller (Generic Desktop) are used:
//   - the first X and Y axis (left stick, or a d-pad reported as axes) -> directions,
//     pressed beyond a quarter of the range from the center;
//   - hat switch -> directions with diagonals;
//   - D-pad up / down / right / left usages -> directions;
//   - Start / Select usages -> Start / Mode;
//   - Button 1..10 -> X A B Y C Z C Z Mode Start: the order of the common
//     SNES / PlayStation style pads (1 top, 2 right, 3 bottom, 4 left, 5 / 6 shoulders,
//     7 / 8 second shoulders, 9 select, 10 start).

#include <stdint.h>

#define HID_AUTO_MAX_FIELDS 24

// Sega button bits, as in SegaController.h
#define HID_SC_CTL_ON 1
#define HID_SC_UP     2
#define HID_SC_DOWN   4
#define HID_SC_LEFT   8
#define HID_SC_RIGHT  16
#define HID_SC_START  32
#define HID_SC_A      64
#define HID_SC_B      128
#define HID_SC_C      256
#define HID_SC_X      512
#define HID_SC_Y      1024
#define HID_SC_Z      2048
#define HID_SC_MODE   4096

enum hid_auto_kind_e : uint8_t {
  HID_AUTO_BUTTON, // non-zero -> btn
  HID_AUTO_AXIS_X,
  HID_AUTO_AXIS_Y,
  HID_AUTO_HAT,
};

typedef struct {
  uint8_t report_id;  // 0: the device has no report ids
  uint8_t kind;       // hid_auto_kind_e
  uint8_t size;       // bits, 1..32
  uint16_t offset;    // bit offset in the report data, after the report id byte
  uint16_t btn;       // HID_AUTO_BUTTON: sega bit
  int32_t min, max;   // logical range
} hid_auto_field_t;

typedef struct {
  uint8_t count;
  hid_auto_field_t fields[HID_AUTO_MAX_FIELDS];
} hid_auto_map_t;

// Build the default map from a report descriptor. Returns false if the device has no
// joystick / gamepad collection with anything to map (out->count is 0 then).
bool hid_auto_map_build(const uint8_t *desc, uint16_t desc_len, hid_auto_map_t *out);

// true if the map has fields in the report with this id (other reports, e.g. battery or
// vendor ones, must not release the buttons)
bool hid_auto_map_has_report(const hid_auto_map_t *m, uint8_t report_id);

// Map one input report to sega bits, starting from base (e.g. SC_CTL_ON). data is the report
// without the report id byte, report_id is that byte (0 if the device has no report ids).
// Fields of other reports and fields beyond len are ignored.
uint16_t hid_auto_map_report(const hid_auto_map_t *m, uint8_t report_id, const uint8_t *data, uint16_t len, uint16_t base);
