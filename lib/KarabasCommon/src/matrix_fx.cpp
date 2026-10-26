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
    case KG_MATRIX_FX_RAIN: return 4;
    case KG_MATRIX_FX_PLASMA: return 3;
    case KG_MATRIX_FX_STARS: return 2;
    case KG_MATRIX_FX_PONG: return 4;
    case KG_MATRIX_FX_DRIVE: return 1;
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

static void pong_serve(matrix_fx_t *fx) {
  uint32_t r = fx_rand(fx);
  fx->ball_x = KG_MATRIX_W / 2;
  fx->ball_y = (2 + r % 4) * 16;
  fx->ball_dx = (r & 0x100) ? 1 : -1;
  fx->ball_dy = (6 + (r >> 9) % 7) * ((r & 0x10000) ? 1 : -1);
  fx->serve_wait = 0;
}

void matrix_fx_init(matrix_fx_t *fx, uint8_t effect, uint8_t color, uint32_t seed) {
  memset(fx, 0, sizeof(*fx));
  fx->effect = effect;
  fx->color = color;
  fx->rnd = seed ? seed : 0x2545F491;
  fx->dir = 1;
  switch (effect) {
    case KG_MATRIX_FX_LIFE: life_seed(fx); break;
    case KG_MATRIX_FX_RAIN: memset(fx->drop, -1, sizeof(fx->drop)); break;
    case KG_MATRIX_FX_STARS:
      for (uint8_t i = 0; i < MATRIX_FX_STARS; i++) {
        uint32_t r = fx_rand(fx);
        fx->star_x[i] = r % (KG_MATRIX_W * 16);
        fx->star_y[i] = (r >> 8) % KG_MATRIX_H;
        fx->star_layer[i] = i % 3;
      }
      break;
    case KG_MATRIX_FX_PONG:
      fx->paddle[0] = fx->paddle[1] = 2;
      pong_serve(fx);
      break;
  }
}

static void draw_digit(matrix_fx_t *fx, uint8_t d, uint8_t x0, uint8_t y0, uint8_t color) {
  for (uint8_t y = 0; y < 5; y++) {
    for (uint8_t x = 0; x < 3; x++) {
      if (digits3x5[d % 10][y] & (4 >> x)) fx->fb[y0 + y][x0 + x] = color;
    }
  }
}

// HH (green) and MM (yellow), the seconds as a red dot running along the bottom row
static void step_clock(matrix_fx_t *fx, const matrix_fx_input_t *t) {
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

// green drops with a yellow head falling down the columns, some of them slower
static void step_rain(matrix_fx_t *fx) {
  fx->step++;
  for (uint8_t x = 0; x < KG_MATRIX_W; x++) {
    int8_t &y = fx->drop[x];
    if (y < 0) {
      uint32_t r = fx_rand(fx);
      if (r % 10 == 0) {
        y = 0;
        if (r & 0x100) fx->drop_slow |= 1 << x;
        else fx->drop_slow &= ~(1 << x);
      }
    } else if (!(fx->drop_slow & (1 << x)) || (fx->step & 1)) {
      y++;
      if (y - 3 >= KG_MATRIX_H) y = -1; // the tail is gone too
    }
    if (y < 0) continue;
    for (int8_t i = 0; i < 4; i++) {
      int8_t ty = y - i;
      if (ty >= 0 && ty < KG_MATRIX_H) fx->fb[ty][x] = i == 0 ? KG_MX_YELLOW : KG_MX_GREEN;
    }
  }
}

// a quarter of a sine, 0..127 over 0..64
static const uint8_t sine_q[65] = {
  0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46, 49, 51, 54, 57, 60, 63, 65, 68,
  71, 73, 76, 78, 81, 83, 85, 88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 112,
  113, 115, 116, 117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127, 127,
};

int8_t matrix_fx_sin8(uint8_t a) {
  uint8_t q = a & 63;
  switch (a >> 6) {
    case 0: return sine_q[q];
    case 1: return sine_q[64 - q];
    case 2: return -sine_q[q];
    default: return -sine_q[64 - q];
  }
}

// three moving sine waves, their sum in bands of off, green, yellow, red
static void step_plasma(matrix_fx_t *fx) {
  static const uint8_t bands[4] = {KG_MX_OFF, KG_MX_GREEN, KG_MX_YELLOW, KG_MX_RED};
  uint8_t t = fx->step++;
  for (uint8_t y = 0; y < KG_MATRIX_H; y++) {
    for (uint8_t x = 0; x < KG_MATRIX_W; x++) {
      int v = matrix_fx_sin8(x * 6 + t * 3) + matrix_fx_sin8(y * 10 - t * 2) + matrix_fx_sin8((x + y) * 5 + t);
      fx->fb[y][x] = bands[((v + 384) / 72) & 3];
    }
  }
}

// stars flying to the left: far ones red and slow, near ones yellow and fast
static void step_stars(matrix_fx_t *fx) {
  static const uint8_t colors[3] = {KG_MX_RED, KG_MX_GREEN, KG_MX_YELLOW};
  static const uint8_t speeds[3] = {4, 8, 16};
  for (uint8_t i = 0; i < MATRIX_FX_STARS; i++) {
    uint8_t l = fx->star_layer[i];
    if (fx->star_x[i] < speeds[l]) {
      fx->star_x[i] += KG_MATRIX_W * 16 - speeds[l];
      fx->star_y[i] = fx_rand(fx) % KG_MATRIX_H;
    } else {
      fx->star_x[i] -= speeds[l];
    }
    uint8_t &p = fx->fb[fx->star_y[i]][fx->star_x[i] / 16];
    if (colors[l] > p) p = colors[l];
  }
}

// a game of two paddles that miss now and then
static void step_pong(matrix_fx_t *fx) {
  if (fx->serve_wait) {
    if (--fx->serve_wait == 0) pong_serve(fx);
  } else {
    // the paddle the ball goes to follows it, not always at once
    uint8_t side = fx->ball_dx < 0 ? 0 : 1;
    int8_t target = fx->ball_y / 16 - 1;
    if (fx_rand(fx) % 4 != 0) {
      if (fx->paddle[side] < target && fx->paddle[side] < KG_MATRIX_H - 3) fx->paddle[side]++;
      else if (fx->paddle[side] > target && fx->paddle[side] > 0) fx->paddle[side]--;
    }
    fx->ball_x += fx->ball_dx;
    fx->ball_y += fx->ball_dy;
    if (fx->ball_y < 0) { fx->ball_y = -fx->ball_y; fx->ball_dy = -fx->ball_dy; }
    if (fx->ball_y > (KG_MATRIX_H - 1) * 16) { fx->ball_y = 2 * (KG_MATRIX_H - 1) * 16 - fx->ball_y; fx->ball_dy = -fx->ball_dy; }
    int8_t edge = side == 0 ? 1 : KG_MATRIX_W - 2;
    if (fx->ball_x == edge) {
      int8_t hit = fx->ball_y / 16 - fx->paddle[side];
      if (hit >= 0 && hit <= 2) {
        fx->ball_dx = -fx->ball_dx;
        fx->ball_dy += (hit - 1) * 5 + (int)(fx_rand(fx) % 5) - 2;
        if (fx->ball_dy > 16) fx->ball_dy = 16;
        if (fx->ball_dy < -16) fx->ball_dy = -16;
      }
    } else if (fx->ball_x <= 0 || fx->ball_x >= KG_MATRIX_W - 1) {
      fx->serve_wait = 10; // missed: the ball stays out for a while
    }
  }
  for (uint8_t i = 0; i < 3; i++) {
    fx->fb[fx->paddle[0] + i][0] = KG_MX_GREEN;
    fx->fb[fx->paddle[1] + i][KG_MATRIX_W - 1] = KG_MX_RED;
  }
  if (fx->ball_x >= 0 && fx->ball_x < KG_MATRIX_W) fx->fb[fx->ball_y / 16][fx->ball_x] = KG_MX_YELLOW;
}

// the mounted images (up to 4): a dot that lights on an access (green read, red write) and
// the head position over the image (yellow); no image: a dotted line
static void step_drive(matrix_fx_t *fx, const matrix_fx_input_t *in) {
  uint8_t slots[4], n = 0;
  for (uint8_t i = 0; i < MATRIX_FX_DRIVES; i++) {
    const matrix_fx_drive_t *d = in ? &in->drive[i] : nullptr;
    if (d && (d->reads != fx->seen_reads[i] || d->writes != fx->seen_writes[i])) {
      fx->led_color[i] = d->writes != fx->seen_writes[i] ? KG_MX_RED : KG_MX_GREEN;
      fx->led_steps[i] = 8;
      fx->seen_reads[i] = d->reads;
      fx->seen_writes[i] = d->writes;
    } else if (fx->led_steps[i]) {
      fx->led_steps[i]--;
    }
    if (d && d->sectors && n < 4) slots[n++] = i;
  }
  if (n == 0) {
    for (uint8_t x = 1; x < KG_MATRIX_W; x += 2) fx->fb[3][x] = KG_MX_RED;
    return;
  }
  uint8_t h = n == 1 ? 4 : n == 2 ? 3 : 2;
  for (uint8_t k = 0; k < n; k++) {
    uint8_t i = slots[k];
    const matrix_fx_drive_t *d = &in->drive[i];
    uint8_t y0 = n == 1 ? 2 : k * (KG_MATRIX_H / n);
    uint32_t pos = (uint32_t) ((uint64_t) d->lba * 13 / d->sectors);
    if (pos > 12) pos = 12;
    for (uint8_t y = y0; y < y0 + h; y++) {
      if (fx->led_steps[i]) fx->fb[y][0] = fx->fb[y][1] = fx->led_color[i];
      fx->fb[y][3 + pos] = KG_MX_YELLOW;
    }
  }
}

void matrix_fx_step(matrix_fx_t *fx, const matrix_fx_input_t *in) {
  switch (fx->effect) {
    case KG_MATRIX_FX_FIRE: step_fire(fx); return;  // fill the whole buffer
    case KG_MATRIX_FX_LIFE: step_life(fx); return;
    case KG_MATRIX_FX_PLASMA: step_plasma(fx); return;
    default: break;
  }
  memset(fx->fb, 0, sizeof(fx->fb));
  switch (fx->effect) {
    case KG_MATRIX_FX_SCANNER: step_scanner(fx); break;
    case KG_MATRIX_FX_RAIN: step_rain(fx); break;
    case KG_MATRIX_FX_STARS: step_stars(fx); break;
    case KG_MATRIX_FX_PONG: step_pong(fx); break;
    case KG_MATRIX_FX_DRIVE: step_drive(fx, in); break;
    default: step_clock(fx, in); break;
  }
}
