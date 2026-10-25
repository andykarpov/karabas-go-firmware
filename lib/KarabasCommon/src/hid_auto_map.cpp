#include "hid_auto_map.h"
#include <string.h>

#define MAX_USAGES 16
#define MAX_REPORT_IDS 8
#define MAX_PUSH 4

// HID usages (HID Usage Tables 1.4)
#define PAGE_DESKTOP 0x01
#define PAGE_BUTTON  0x09
#define DESKTOP_JOYSTICK   0x04
#define DESKTOP_GAMEPAD    0x05
#define DESKTOP_MULTI_AXIS 0x08
#define DESKTOP_X 0x30
#define DESKTOP_Y 0x31
#define DESKTOP_HAT 0x39
#define DESKTOP_START 0x3D
#define DESKTOP_SELECT 0x3E
#define DESKTOP_DPAD_UP 0x90
#define DESKTOP_DPAD_DOWN 0x91
#define DESKTOP_DPAD_RIGHT 0x92
#define DESKTOP_DPAD_LEFT 0x93

// Button 1..10, see the header
static const uint16_t button_map[] = {
  HID_SC_X, HID_SC_A, HID_SC_B, HID_SC_Y, HID_SC_C, HID_SC_Z, HID_SC_C, HID_SC_Z, HID_SC_MODE, HID_SC_START
};

typedef struct {
  uint16_t usage_page;
  int32_t log_min, log_max;
  uint32_t report_size, report_count;
  uint8_t report_id;
} globals_t;

typedef struct {
  hid_auto_map_t *out;
  globals_t g;
  globals_t stack[MAX_PUSH];
  uint8_t sp;
  // locals, cleared after each main item
  uint32_t usages[MAX_USAGES];
  uint8_t n_usages;
  uint32_t usage_min, usage_max;
  bool has_min, has_max;
  // collections
  uint8_t depth;
  uint32_t app_usage; // usage of the current top-level application collection
  // input bit offset per report id
  struct { uint8_t id; uint32_t bits; } offsets[MAX_REPORT_IDS];
  uint8_t n_offsets;
  bool has_x, has_y, has_hat;
} parser_t;

static uint32_t *offset_of(parser_t &p, uint8_t id) {
  for (uint8_t i = 0; i < p.n_offsets; i++)
    if (p.offsets[i].id == id) return &p.offsets[i].bits;
  if (p.n_offsets == MAX_REPORT_IDS) return nullptr;
  p.offsets[p.n_offsets].id = id;
  p.offsets[p.n_offsets].bits = 0;
  return &p.offsets[p.n_offsets++].bits;
}

static void clear_locals(parser_t &p) {
  p.n_usages = 0;
  p.has_min = p.has_max = false;
}

// usage of the i-th field of a main item
static uint32_t usage_at(const parser_t &p, uint32_t i) {
  if (p.n_usages) return p.usages[i < p.n_usages ? i : p.n_usages - 1];
  if (p.has_min) {
    uint32_t u = p.usage_min + i;
    if (p.has_max && u > p.usage_max) u = p.usage_max;
    return u;
  }
  return 0;
}

static void add_field(parser_t &p, uint8_t kind, uint16_t btn, uint32_t offset) {
  hid_auto_map_t *m = p.out;
  if (m->count == HID_AUTO_MAX_FIELDS || offset > 0xffff) return;
  hid_auto_field_t &f = m->fields[m->count++];
  f.report_id = p.g.report_id;
  f.kind = kind;
  f.size = (uint8_t) p.g.report_size;
  f.offset = (uint16_t) offset;
  f.btn = btn;
  f.min = p.g.log_min;
  f.max = p.g.log_max;
}

static void classify(parser_t &p, uint32_t usage, uint32_t offset) {
  uint16_t page = usage >> 16, id = usage & 0xffff;
  if (page == PAGE_BUTTON) {
    if (id >= 1 && id <= sizeof(button_map) / sizeof(button_map[0]))
      add_field(p, HID_AUTO_BUTTON, button_map[id - 1], offset);
    return;
  }
  if (page != PAGE_DESKTOP) return;
  switch (id) {
    case DESKTOP_X:   if (!p.has_x)   { p.has_x = true;   add_field(p, HID_AUTO_AXIS_X, 0, offset); } break;
    case DESKTOP_Y:   if (!p.has_y)   { p.has_y = true;   add_field(p, HID_AUTO_AXIS_Y, 0, offset); } break;
    case DESKTOP_HAT: if (!p.has_hat) { p.has_hat = true; add_field(p, HID_AUTO_HAT, 0, offset); } break;
    case DESKTOP_DPAD_UP:    add_field(p, HID_AUTO_BUTTON, HID_SC_UP, offset); break;
    case DESKTOP_DPAD_DOWN:  add_field(p, HID_AUTO_BUTTON, HID_SC_DOWN, offset); break;
    case DESKTOP_DPAD_RIGHT: add_field(p, HID_AUTO_BUTTON, HID_SC_RIGHT, offset); break;
    case DESKTOP_DPAD_LEFT:  add_field(p, HID_AUTO_BUTTON, HID_SC_LEFT, offset); break;
    case DESKTOP_START:      add_field(p, HID_AUTO_BUTTON, HID_SC_START, offset); break;
    case DESKTOP_SELECT:     add_field(p, HID_AUTO_BUTTON, HID_SC_MODE, offset); break;
  }
}

static bool is_pad(uint32_t app_usage) {
  return (app_usage >> 16) == PAGE_DESKTOP &&
    ((app_usage & 0xffff) == DESKTOP_JOYSTICK || (app_usage & 0xffff) == DESKTOP_GAMEPAD ||
     (app_usage & 0xffff) == DESKTOP_MULTI_AXIS);
}

static void input_item(parser_t &p, uint32_t flags) {
  uint32_t *offset = offset_of(p, p.g.report_id);
  if (!offset) return;
  bool constant = flags & 1, variable = flags & 2;
  uint32_t size = p.g.report_size, count = p.g.report_count;
  if (!constant && variable && size >= 1 && size <= 32 && is_pad(p.app_usage)) {
    for (uint32_t i = 0; i < count; i++) classify(p, usage_at(p, i), *offset + i * size);
  }
  *offset += size * count;
}

bool hid_auto_map_build(const uint8_t *desc, uint16_t desc_len, hid_auto_map_t *out) {
  static parser_t p; // kept off the task stack; called from the USB task only
  memset(&p, 0, sizeof(p));
  memset(out, 0, sizeof(*out));
  p.out = out;

  uint16_t i = 0;
  while (i < desc_len) {
    uint8_t prefix = desc[i++];
    if (prefix == 0xFE) { // long item: size, tag, data
      if (i + 1 >= desc_len) break;
      i += 2 + desc[i];
      continue;
    }
    uint8_t size = prefix & 3;
    if (size == 3) size = 4;
    uint8_t type = (prefix >> 2) & 3, tag = prefix >> 4;
    if (i + size > desc_len) break;
    uint32_t udata = 0;
    for (uint8_t b = 0; b < size; b++) udata |= (uint32_t) desc[i + b] << (8 * b);
    int32_t sdata = (int32_t) udata;
    if (size == 1) sdata = (int8_t) udata;
    else if (size == 2) sdata = (int16_t) udata;
    i += size;

    if (type == 0) { // main
      switch (tag) {
        case 0x8: input_item(p, udata); break;
        case 0xA: // collection
          if (p.depth == 0 && udata == 1) p.app_usage = p.n_usages ? p.usages[0] : 0;
          p.depth++;
          break;
        case 0xC: // end collection
          if (p.depth) p.depth--;
          if (p.depth == 0) p.app_usage = 0;
          break;
      }
      clear_locals(p);
    } else if (type == 1) { // global
      switch (tag) {
        case 0x0: p.g.usage_page = (uint16_t) udata; break;
        case 0x1: p.g.log_min = sdata; break;
        // a max written as one byte 0xff means 255 to most devices, not -1
        case 0x2: p.g.log_max = (sdata < p.g.log_min) ? (int32_t) udata : sdata; break;
        case 0x7: p.g.report_size = udata; break;
        case 0x8: p.g.report_id = (uint8_t) udata; break;
        case 0x9: p.g.report_count = udata; break;
        case 0xA: if (p.sp < MAX_PUSH) p.stack[p.sp++] = p.g; break;
        case 0xB: if (p.sp) p.g = p.stack[--p.sp]; break;
      }
    } else if (type == 2) { // local
      uint32_t usage = (size == 4) ? udata : ((uint32_t) p.g.usage_page << 16) | udata;
      switch (tag) {
        case 0x0: if (p.n_usages < MAX_USAGES) p.usages[p.n_usages++] = usage; break;
        case 0x1: p.usage_min = usage; p.has_min = true; break;
        case 0x2: p.usage_max = usage; p.has_max = true; break;
      }
    }
  }
  return out->count > 0;
}

// read size bits at a bit offset, lsb first; false if the field is beyond the data
static bool read_bits(const uint8_t *data, uint16_t len, uint32_t offset, uint8_t size, uint32_t &out) {
  if ((offset + size + 7) / 8 > len) return false;
  out = 0;
  for (uint8_t b = 0; b < size; b++) {
    uint32_t bit = offset + b;
    if ((data[bit >> 3] >> (bit & 7)) & 1) out |= (uint32_t) 1 << b;
  }
  return true;
}

// hat positions 0..7 from north, clockwise
static const uint16_t hat_dirs[8] = {
  HID_SC_UP, HID_SC_UP | HID_SC_RIGHT, HID_SC_RIGHT, HID_SC_DOWN | HID_SC_RIGHT,
  HID_SC_DOWN, HID_SC_DOWN | HID_SC_LEFT, HID_SC_LEFT, HID_SC_UP | HID_SC_LEFT
};

bool hid_auto_map_has_report(const hid_auto_map_t *m, uint8_t report_id) {
  for (uint8_t i = 0; i < m->count; i++)
    if (m->fields[i].report_id == report_id) return true;
  return false;
}

uint16_t hid_auto_map_report(const hid_auto_map_t *m, uint8_t report_id, const uint8_t *data, uint16_t len, uint16_t base) {
  uint16_t res = base;
  for (uint8_t i = 0; i < m->count; i++) {
    const hid_auto_field_t &f = m->fields[i];
    uint32_t raw;
    if (f.report_id != report_id || !read_bits(data, len, f.offset, f.size, raw)) continue;
    int64_t v = raw;
    if (f.min < 0 && f.size < 32 && (raw >> (f.size - 1)) & 1) v -= (int64_t) 1 << f.size; // sign-extend
    else if (f.min < 0 && f.size == 32) v = (int32_t) raw;
    switch (f.kind) {
      case HID_AUTO_BUTTON:
        if (v) res |= f.btn;
        break;
      case HID_AUTO_AXIS_X:
      case HID_AUTO_AXIS_Y: {
        int64_t range = (int64_t) f.max - f.min;
        if (range <= 0) break;
        // compare doubled values: center is (min + max) / 2, the dead zone a quarter of the range
        int64_t center2 = (int64_t) f.min + f.max, v2 = 2 * v, dead2 = range / 2;
        bool x = (f.kind == HID_AUTO_AXIS_X);
        if (v2 < center2 - dead2) res |= x ? HID_SC_LEFT : HID_SC_UP;
        else if (v2 > center2 + dead2) res |= x ? HID_SC_RIGHT : HID_SC_DOWN;
        break;
      }
      case HID_AUTO_HAT: {
        if (v < f.min || v > f.max) break; // out of range: centered (null state)
        int64_t n = (int64_t) f.max - f.min + 1, pos = v - f.min;
        if (n == 8) res |= hat_dirs[pos];
        else if (n == 4) res |= hat_dirs[pos * 2];
        break;
      }
    }
  }
  return res;
}
