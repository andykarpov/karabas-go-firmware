#include "hid_joy_map.h"
#include <stdlib.h>

// parse a decimal number, advance s past it. returns false if there is no number
static bool parse_num(const char *&s, long &out) {
  while (*s == ' ') s++;
  char *end;
  out = strtol(s, &end, 10);
  if (end == s) return false;
  s = end;
  while (*s == ' ') s++;
  return true;
}

bool hid_joy_button_parse(const char *value, hid_joy_button_config_t *out) {
  long fields[3] = {0, 0, 0};
  const char *s = value;
  uint8_t n = 0;
  while (n < 3) {
    if (!parse_num(s, fields[n])) return false;
    n++;
    if (*s == ',') { s++; continue; }
    if (*s == '\0') break;
    return false;
  }
  if (*s != '\0') return false; // more than 3 fields
  if (fields[0] < 0 || fields[0] > 255 || fields[2] < 0 || fields[2] > 255) return false;
  if (fields[1] == 1 && fields[2] > 7) return false;
  out->pos = (uint8_t) fields[0];
  out->isBit = (fields[1] == 1);
  out->val = (uint8_t) fields[2];
  out->isAvail = true;
  return true;
}

uint16_t hid_joy_map_report(const hid_joy_config_t *cfg, const uint8_t *report, uint16_t len, uint16_t base) {
  uint16_t res = base;
  for (uint8_t i = 0; i < HID_JOY_BUTTONS; i++) {
    const hid_joy_button_config_t *b = &cfg->buttons[i];
    if (!b->isAvail || b->pos >= len) continue;
    bool pressed = b->isBit ? ((report[b->pos] >> b->val) & 1) : (report[b->pos] == b->val);
    if (pressed) res |= b->btn;
  }
  return res;
}
