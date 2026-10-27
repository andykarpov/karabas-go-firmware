#include "matrix_meter.h"
#include <string.h>

void kg_meter_init(kg_meter_t *m) {
  memset(m, 0, sizeof(*m));
}

uint8_t kg_meter_level(uint16_t v) {
  static const uint16_t steps[KG_METER_MAX] = {20000, 16000, 10000, 8000, 6500, 5000, 3500, 3000, 2000, 1500, 1000, 750, 600, 500, 350, 200};
  for (uint8_t i = 0; i < KG_METER_MAX; i++) {
    if (v >= steps[i]) return KG_METER_MAX - i;
  }
  return 0;
}

uint16_t kg_meter_decay(uint16_t v, uint8_t decay) {
  uint8_t shift = decay == KG_MX_DECAY_SLOW ? 5 : decay == KG_MX_DECAY_FAST ? 3 : 4;
  uint16_t d = v >> shift;
  if (d == 0 && v > 0) d = 1;
  return v - d;
}

// the color of cell i of n lit up to lvl
static uint8_t cell_color(uint8_t colors, uint8_t i, uint8_t n, uint8_t lvl) {
  switch (colors) {
    case KG_MX_METER_COLORS_GREEN: return KG_MX_GREEN;
    case KG_MX_METER_COLORS_YELLOW: return KG_MX_YELLOW;
    case KG_MX_METER_COLORS_RED: return KG_MX_RED;
    case KG_MX_METER_COLORS_FIRE: return i + 1 == lvl ? KG_MX_RED : KG_MX_YELLOW;
  }
  // zones: of 16 cells, 13 and 14 yellow, 15 red
  uint8_t pos = (i * KG_METER_MAX + KG_METER_MAX - 1) / n;
  return pos > 14 ? KG_MX_RED : pos > 12 ? KG_MX_YELLOW : KG_MX_GREEN;
}

static uint8_t peak_color(uint8_t colors) {
  return colors == KG_MX_METER_COLORS_RED ? KG_MX_YELLOW : KG_MX_RED;
}

static void peak_move(kg_meter_t *m, uint8_t ch, uint8_t lvl) {
  if (lvl >= m->peak[ch]) {
    m->peak[ch] = lvl;
    m->hold[ch] = KG_METER_HOLD_FRAMES;
    m->fall[ch] = KG_METER_FALL_FRAMES;
  } else if (m->hold[ch] > 0) {
    m->hold[ch]--;
  } else if (--m->fall[ch] == 0) {
    m->peak[ch]--;
    m->fall[ch] = KG_METER_FALL_FRAMES;
  }
}

typedef uint8_t fb_t[KG_MATRIX_H][KG_MATRIX_W];

// a horizontal bar of 16 cells over the rows y0..y1, from the left
static void bar_right(fb_t fb, uint8_t y0, uint8_t y1, uint8_t lvl, uint8_t peak, const kg_mx_style_t *st) {
  for (uint8_t y = y0; y <= y1; y++) {
    for (uint8_t i = 0; i < lvl; i++) fb[y][i] = cell_color(st->opt[KG_MX_OPT_METER_COLORS], i, KG_METER_MAX, lvl);
    if (st->meter_peak && peak > lvl) fb[y][peak - 1] = peak_color(st->opt[KG_MX_OPT_METER_COLORS]);
  }
}

// a half bar of 8 cells on all rows, from the middle to the left (dir -1) or the right (dir 1)
static void bar_half(fb_t fb, int8_t dir, uint8_t lvl, uint8_t peak, const kg_mx_style_t *st) {
  uint8_t n = KG_MATRIX_W / 2, l = (lvl + 1) / 2, p = (peak + 1) / 2;
  for (uint8_t y = 0; y < KG_MATRIX_H; y++) {
    for (uint8_t i = 0; i < l; i++) {
      uint8_t x = dir < 0 ? n - 1 - i : n + i;
      fb[y][x] = cell_color(st->opt[KG_MX_OPT_METER_COLORS], i, n, l);
    }
    if (st->meter_peak && p > l) fb[y][dir < 0 ? n - p : n + p - 1] = peak_color(st->opt[KG_MX_OPT_METER_COLORS]);
  }
}

// a column of 8 cells over the columns x0..x1, from the bottom
static void bar_up(fb_t fb, uint8_t x0, uint8_t x1, uint8_t lvl, uint8_t peak, const kg_mx_style_t *st) {
  uint8_t n = KG_MATRIX_H, l = (lvl + 1) / 2, p = (peak + 1) / 2;
  for (uint8_t x = x0; x <= x1; x++) {
    for (uint8_t i = 0; i < l; i++) fb[n - 1 - i][x] = cell_color(st->opt[KG_MX_OPT_METER_COLORS], i, n, l);
    if (st->meter_peak && p > l) fb[n - p][x] = peak_color(st->opt[KG_MX_OPT_METER_COLORS]);
  }
}

void kg_meter_draw(kg_meter_t *m, const kg_mx_style_t *st, uint8_t left, uint8_t right, fb_t fb) {
  if (left > KG_METER_MAX) left = KG_METER_MAX;
  if (right > KG_METER_MAX) right = KG_METER_MAX;
  memset(fb, KG_MX_OFF, sizeof(fb_t));
  uint8_t style = st->opt[KG_MX_OPT_METER_STYLE];
  if (style == KG_MX_METER_MONO) {
    uint8_t lvl = left > right ? left : right;
    peak_move(m, 0, lvl);
    bar_right(fb, 0, KG_MATRIX_H - 1, lvl, m->peak[0], st);
    return;
  }
  peak_move(m, 0, left);
  peak_move(m, 1, right);
  switch (style) {
    case KG_MX_METER_WIDE:
      bar_right(fb, 0, 3, left, m->peak[0], st);
      bar_right(fb, 4, 7, right, m->peak[1], st);
      break;
    case KG_MX_METER_CENTER:
      bar_half(fb, -1, left, m->peak[0], st);
      bar_half(fb, 1, right, m->peak[1], st);
      break;
    case KG_MX_METER_VERTICAL:
      bar_up(fb, 0, 6, left, m->peak[0], st);
      bar_up(fb, 9, 15, right, m->peak[1], st);
      break;
    default: // bars
      bar_right(fb, 1, 2, left, m->peak[0], st);
      bar_right(fb, 5, 6, right, m->peak[1], st);
  }
}
