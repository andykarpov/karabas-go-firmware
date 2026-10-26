#include "matrix_fx.h"

#include <string.h>

// 3x5 digits, a row per byte (bits 2..0 = x 0..2)
static const uint8_t digits3x5[10][5] = {
  {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
  {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

static uint32_t fx_rand(matrix_fx_t *fx) {
  uint32_t x = fx->rnd;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  fx->rnd = x;
  return x;
}

uint8_t matrix_fx_default_speed(uint8_t effect) {
  switch (effect) {
    case KG_MATRIX_FX_FIRE: return 3;
    case KG_MATRIX_FX_LIFE: return 10;
    case KG_MATRIX_FX_SCANNER: return 3;
    default: return 5;
  }
}

static void life_seed(matrix_fx_t *fx) {
  for (uint8_t y = 0; y < KG_MATRIX_H; y++) {
    fx->cells[y] = 0;
    for (uint8_t x = 0; x < KG_MATRIX_W; x++) {
      if (fx_rand(fx) % 100 < 35) fx->cells[y] |= 1 << x;
    }
    fx->prev[y] = fx->prev2[y] = 0;
  }
  memset(fx->age, 0, sizeof(fx->age));
  fx->still = 0;
  fx->step = 0;
}

void matrix_fx_init(matrix_fx_t *fx, uint8_t effect, uint8_t color, uint32_t seed) {
  memset(fx, 0, sizeof(*fx));
  fx->effect = effect;
  fx->color = color;
  fx->rnd = seed ? seed : 0x2545F491;
  fx->dir = 1;
  if (effect == KG_MATRIX_FX_LIFE) life_seed(fx);
}

static void draw_digit(matrix_fx_t *fx, uint8_t d, uint8_t x0, uint8_t y0, uint8_t color) {
  for (uint8_t y = 0; y < 5; y++) {
    for (uint8_t x = 0; x < 3; x++) {
      if (digits3x5[d % 10][y] & (4 >> x)) fx->fb[y0 + y][x0 + x] = color;
    }
  }
}

// HH (green) and MM (yellow), the seconds as a red dot running along the bottom row
static void step_clock(matrix_fx_t *fx, const matrix_fx_time_t *t) {
  uint8_t h = t ? t->hour : 0, m = t ? t->minute : 0, s = t ? t->second : 0;
  draw_digit(fx, h / 10, 0, 1, KG_MX_GREEN);
  draw_digit(fx, h % 10, 4, 1, KG_MX_GREEN);
  draw_digit(fx, m / 10, 9, 1, KG_MX_YELLOW);
  draw_digit(fx, m % 10, 13, 1, KG_MX_YELLOW);
  fx->fb[7][(s % 60) * KG_MATRIX_W / 60] = KG_MX_RED;
}

// the heat rises from the bottom row and cools down
static void step_fire(matrix_fx_t *fx) {
  for (uint8_t x = 0; x < KG_MATRIX_W; x++) {
    uint32_t r = fx_rand(fx);
    fx->heat[KG_MATRIX_H - 1][x] = (r % 8 == 0) ? 4 : 9 + (r >> 8) % 7;
  }
  for (uint8_t y = 0; y < KG_MATRIX_H - 1; y++) {
    for (uint8_t x = 0; x < KG_MATRIX_W; x++) {
      uint8_t l = fx->heat[y + 1][x > 0 ? x - 1 : x];
      uint8_t c = fx->heat[y + 1][x];
      uint8_t r = fx->heat[y + 1][x < KG_MATRIX_W - 1 ? x + 1 : x];
      int v = (l + 2 * c + r) / 4 - (int)(fx_rand(fx) % 4);
      fx->heat[y][x] = v > 0 ? v : 0;
    }
  }
  for (uint8_t y = 0; y < KG_MATRIX_H; y++) {
    for (uint8_t x = 0; x < KG_MATRIX_W; x++) {
      uint8_t h = fx->heat[y][x];
      fx->fb[y][x] = h >= 10 ? KG_MX_YELLOW : h >= 4 ? KG_MX_RED : KG_MX_OFF;
    }
  }
}

void matrix_fx_life_next(const uint16_t *cells, uint16_t *out) {
  for (uint8_t y = 0; y < KG_MATRIX_H; y++) {
    out[y] = 0;
    for (uint8_t x = 0; x < KG_MATRIX_W; x++) {
      uint8_t n = 0;
      for (int8_t dy = -1; dy <= 1; dy++) {
        uint16_t row = cells[(y + dy + KG_MATRIX_H) % KG_MATRIX_H];
        for (int8_t dx = -1; dx <= 1; dx++) {
          if (dx == 0 && dy == 0) continue;
          if (row & (1 << ((x + dx + KG_MATRIX_W) % KG_MATRIX_W))) n++;
        }
      }
      bool alive = cells[y] & (1 << x);
      if (n == 3 || (alive && n == 2)) out[y] |= 1 << x;
    }
  }
}

// new cells green, older ones yellow, the ones that just died red; a field that stands
// (or blinks with period 2) for a while, or dies out, is seeded again
static void step_life(matrix_fx_t *fx) {
  uint16_t next[KG_MATRIX_H];
  matrix_fx_life_next(fx->cells, next);
  memcpy(fx->prev2, fx->prev, sizeof(fx->prev));
  memcpy(fx->prev, fx->cells, sizeof(fx->cells));
  memcpy(fx->cells, next, sizeof(next));
  fx->step++;

  bool empty = true;
  for (uint8_t y = 0; y < KG_MATRIX_H; y++) {
    if (fx->cells[y]) empty = false;
  }
  bool stands = memcmp(fx->cells, fx->prev, sizeof(fx->cells)) == 0 || memcmp(fx->cells, fx->prev2, sizeof(fx->cells)) == 0;
  fx->still = stands ? fx->still + 1 : 0;

  for (uint8_t y = 0; y < KG_MATRIX_H; y++) {
    for (uint8_t x = 0; x < KG_MATRIX_W; x++) {
      bool alive = fx->cells[y] & (1 << x);
      bool was = fx->prev[y] & (1 << x);
      if (alive) {
        if (fx->age[y][x] < 255) fx->age[y][x]++;
        fx->fb[y][x] = fx->age[y][x] > 1 ? KG_MX_YELLOW : KG_MX_GREEN;
      } else {
        fx->age[y][x] = 0;
        fx->fb[y][x] = was ? KG_MX_RED : KG_MX_OFF;
      }
    }
  }

  if (empty || fx->still > 15 || fx->step > 500) life_seed(fx);
}

// a dot running from side to side with a tail
static void step_scanner(matrix_fx_t *fx) {
  uint8_t color = fx->color ? fx->color : KG_MX_RED;
  for (int8_t i = 2; i >= 0; i--) {
    int8_t x = fx->pos - i * fx->dir;
    if (x < 0 || x >= KG_MATRIX_W) continue;
    uint8_t top = 2 + i, bottom = 5 - i; // the head is 4 rows high, the tail lower
    if (i == 2) top = bottom = 3;
    for (uint8_t y = top; y <= bottom; y++) fx->fb[y][x] = color;
  }
  if ((fx->pos == KG_MATRIX_W - 1 && fx->dir > 0) || (fx->pos == 0 && fx->dir < 0)) fx->dir = -fx->dir;
  fx->pos += fx->dir;
}

void matrix_fx_step(matrix_fx_t *fx, const matrix_fx_time_t *time) {
  switch (fx->effect) {
    case KG_MATRIX_FX_FIRE: step_fire(fx); return;  // fills the whole buffer
    case KG_MATRIX_FX_LIFE: step_life(fx); return;
    default: break;
  }
  memset(fx->fb, 0, sizeof(fx->fb));
  if (fx->effect == KG_MATRIX_FX_SCANNER) step_scanner(fx);
  else step_clock(fx, time);
}
