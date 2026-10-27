#include "hid_joy_map.h"
#include <stdlib.h>

// parse a decimal number, advance s past it. returns false if there is no number
static bool parse_num(const char **s, long *out) {
  while (**s == ' ') (*s)++;
  char *end;
  *out = strtol(*s, &end, 10);
  if (end == *s) return false;
  *s = end;
  while (**s == ' ') (*s)++;
  return true;
}

// parse one "pos,type,value[,arg]" source up to '|' or the end
static bool parse_rule(const char **sp, hid_joy_rule_t *out) {
  const char *s = *sp;
  long f[4] = {0, 0, 0, 0};
  uint8_t n = 0;
  while (n < 4) {
    if (!parse_num(&s, &f[n])) return false;
    n++;
    if (*s == ',') { s++; continue; }
    if (*s == '\0' || *s == '|') break;
    return false;
  }
  if (*s != '\0' && *s != '|') return false; // more than 4 fields
  for (uint8_t i = 0; i < 4; i++)
    if (f[i] < 0 || f[i] > 255) return false;
  switch (f[1]) {
    case HID_JOY_VALUE:
      if (n > 3) return false;
      break;
    case HID_JOY_BIT:
    case HID_JOY_BIT_CLEAR:
      if (n > 3 || f[2] > 7) return false;
      break;
    case HID_JOY_HAT:
      if (f[2] > 7 || f[3] > 4) return false;
      break;
    case HID_JOY_RANGE:
      if (n < 4 || f[2] > f[3]) return false;
      break;
    default:
      return false;
  }
  out->pos = (uint8_t) f[0];
  out->type = (uint8_t) f[1];
  out->val = (uint8_t) f[2];
  out->arg = (uint8_t) f[3];
  *sp = s;
  return true;
}

bool hid_joy_rules_parse(const char *value, uint16_t btn, hid_joy_config_t *cfg) {
  const char *s = value;
  uint8_t count = cfg->count;
  for (;;) {
    if (count == HID_JOY_MAX_RULES) return false;
    hid_joy_rule_t *r = &cfg->rules[count];
    if (!parse_rule(&s, r)) return false;
    r->btn = btn;
    count++;
    if (*s == '\0') break;
    s++; // '|'
  }
  cfg->count = count;
  return true;
}

bool hid_joy_accepts_report(const hid_joy_config_t *cfg, uint8_t report_id) {
  return cfg->report_id < 0 || cfg->report_id == report_id;
}

static bool rule_pressed(const hid_joy_rule_t *r, uint8_t b) {
  switch (r->type) {
    case HID_JOY_VALUE: return b == r->val;
    case HID_JOY_BIT: return (b >> r->val) & 1;
    case HID_JOY_BIT_CLEAR: return !((b >> r->val) & 1);
    case HID_JOY_RANGE: return b >= r->val && b <= r->arg;
    case HID_JOY_HAT: {
      uint8_t hat = (b >> r->arg) & 0x0f;
      if (hat > 7) return false; // centered
      uint8_t d = (hat - r->val) & 7;
      return d == 0 || d == 1 || d == 7;
    }
  }
  return false;
}

uint16_t hid_joy_map_report(const hid_joy_config_t *cfg, const uint8_t *report, uint16_t len, uint16_t base) {
  uint16_t res = base;
  for (uint8_t i = 0; i < cfg->count; i++) {
    const hid_joy_rule_t *r = &cfg->rules[i];
    if (r->pos < len && rule_pressed(r, report[r->pos])) res |= r->btn;
  }
  return res;
}
