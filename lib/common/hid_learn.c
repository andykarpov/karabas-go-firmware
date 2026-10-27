#include "hid_learn.h"
#include <stdio.h>
#include <string.h>

// a byte at rest is a little further from its rest range before it counts as moved
#define REST_MARGIN 0x18

void hid_learn_rest_reset(hid_learn_rest_t *rest) {
  memset(rest, 0, sizeof(*rest));
}

void hid_learn_rest_add(hid_learn_rest_t *rest, uint8_t report_id, const uint8_t *data, uint16_t len) {
  if (len > HID_LEARN_MAX_REPORT) len = HID_LEARN_MAX_REPORT;
  if (len == 0) return;
  if (rest->len == 0) {
    rest->len = len;
    rest->report_id = report_id;
    for (uint16_t i = 0; i < len; i++) {
      rest->umin[i] = rest->umax[i] = data[i];
      rest->smin[i] = rest->smax[i] = (int8_t) data[i];
    }
  }
  if (report_id != rest->report_id || len != rest->len) return;
  for (uint16_t i = 0; i < len; i++) {
    uint8_t u = data[i];
    int8_t s = (int8_t) u;
    if (u < rest->umin[i]) rest->umin[i] = u;
    if (u > rest->umax[i]) rest->umax[i] = u;
    if (s < rest->smin[i]) rest->smin[i] = s;
    if (s > rest->smax[i]) rest->smax[i] = s;
  }
  memcpy(rest->last, data, len);
}

static bool same_report(const hid_learn_rest_t *rest, uint8_t report_id, uint16_t len) {
  if (len > HID_LEARN_MAX_REPORT) len = HID_LEARN_MAX_REPORT;
  return rest->len && report_id == rest->report_id && len == rest->len;
}

static int beyond(int lo, int hi, int v) {
  return v < lo ? lo - v : v > hi ? v - hi : 0;
}

bool hid_learn_rest_moved(const hid_learn_rest_t *rest, uint8_t report_id, const uint8_t *data, uint16_t len) {
  if (!same_report(rest, report_id, len)) return false;
  for (uint16_t i = 0; i < rest->len; i++) {
    if (beyond(rest->umin[i], rest->umax[i], data[i]) > REST_MARGIN &&
        beyond(rest->smin[i], rest->smax[i], (int8_t) data[i]) > REST_MARGIN) return true;
  }
  return false;
}

// a byte that jitters around 0 has a small signed range and a huge unsigned one
static bool is_signed(const hid_learn_rest_t *rest, uint16_t i) {
  return (rest->smax[i] - rest->smin[i]) < (rest->umax[i] - rest->umin[i]);
}

static bool is_stable(const hid_learn_rest_t *rest, uint16_t i) {
  return rest->umin[i] == rest->umax[i];
}

// how far v is beyond the rest range [lo, hi], and the range rule for it
static int axis_move(int lo, int hi, int v, bool sgn, hid_joy_rule_t *r) {
  int dmin = sgn ? -128 : 0, dmax = sgn ? 127 : 255;
  int from, to, dist;
  if (v < lo - HID_LEARN_AXIS_MIN) {
    int t = (lo + v) / 2;
    if (sgn && lo >= 0 && t >= 0) t = -1; // keep a signed range on one side of 0
    from = dmin; to = t; dist = lo - v;
  } else if (v > hi + HID_LEARN_AXIS_MIN) {
    int t = (hi + v + 1) / 2;
    if (sgn && hi <= 0 && t <= 0) t = 1;
    from = t; to = dmax; dist = v - hi;
  } else {
    return 0;
  }
  r->type = HID_JOY_RANGE;
  r->val = (uint8_t) from;
  r->arg = (uint8_t) to;
  return dist;
}

bool hid_learn_detect(const hid_learn_rest_t *rest, uint8_t report_id, const uint8_t *data, uint16_t len, hid_joy_rule_t *out) {
  if (!same_report(rest, report_id, len)) return false;
  len = rest->len;

  // a hat, then a single bit, in bytes that do not jitter
  for (uint8_t pass = 0; pass < 2; pass++) {
    for (uint16_t i = 0; i < len; i++) {
      if (!is_stable(rest, i)) continue;
      uint8_t b = rest->last[i], c = data[i];
      if (b == c) continue;
      if (pass == 0) {
        for (uint8_t shift = 0; shift <= 4; shift += 4) {
          uint8_t bn = (b >> shift) & 0x0f, cn = (c >> shift) & 0x0f;
          // the other nibble (buttons) stays, so a whole axis byte moving is not a hat
          bool other_same = ((b ^ c) & (0xf0 >> shift)) == 0;
          if (bn >= 8 && cn <= 7 && other_same) {
            *out = (hid_joy_rule_t) {0, (uint8_t) i, HID_JOY_HAT, cn, shift};
            return true;
          }
        }
      } else {
        uint8_t d = b ^ c;
        // a centered byte (an axis or a d-pad reported as one) is an axis even if one bit moved
        bool centered = b >= 0x60 && b <= 0x9f;
        if (!centered && (d & (d - 1)) == 0) {
          uint8_t bit = 0;
          while (!((d >> bit) & 1)) bit++;
          *out = (hid_joy_rule_t) {0, (uint8_t) i, (uint8_t) ((c & d) ? HID_JOY_BIT : HID_JOY_BIT_CLEAR), bit, 0};
          return true;
        }
      }
    }
  }

  // the axis that moved most
  int best = 0;
  hid_joy_rule_t r = {0, 0, 0, 0, 0};
  for (uint16_t i = 0; i < len; i++) {
    bool sgn = is_signed(rest, i);
    int lo = sgn ? rest->smin[i] : rest->umin[i];
    int hi = sgn ? rest->smax[i] : rest->umax[i];
    int v = sgn ? (int8_t) data[i] : data[i];
    hid_joy_rule_t cand = {0, (uint8_t) i, 0, 0, 0};
    int dist = axis_move(lo, hi, v, sgn, &cand);
    if (dist > best) { best = dist; r = cand; }
  }
  if (!best) return false;
  *out = r;
  return true;
}

bool hid_learn_is_rest(const hid_learn_rest_t *rest, uint8_t report_id, const uint8_t *data, uint16_t len) {
  if (!same_report(rest, report_id, len)) return false;
  for (uint16_t i = 0; i < rest->len; i++) {
    if (is_stable(rest, i)) {
      if (data[i] != rest->last[i]) return false;
      continue;
    }
    bool sgn = is_signed(rest, i);
    int lo = sgn ? rest->smin[i] : rest->umin[i];
    int hi = sgn ? rest->smax[i] : rest->umax[i];
    int v = sgn ? (int8_t) data[i] : data[i];
    if (v < lo - REST_MARGIN || v > hi + REST_MARGIN) return false;
  }
  return true;
}

int hid_learn_format_rule(const hid_joy_rule_t *r, char *buf, size_t size) {
  if (r->type == HID_JOY_RANGE || (r->type == HID_JOY_HAT && r->arg))
    return snprintf(buf, size, "%u,%u,%u,%u", r->pos, r->type, r->val, r->arg);
  return snprintf(buf, size, "%u,%u,%u", r->pos, r->type, r->val);
}
