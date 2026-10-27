#pragma once

// Effects of the LED matrix (make_core "matrix": {"mode": "effect"}), drawn into a 16x8
// buffer of KG_MX_* colors. Pure C, no Arduino dependencies.

#include <stdbool.h>
#include <stdint.h>

#include "kg_matrix.h"
#include "matrix_style.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MATRIX_FX_DRIVES 8  // image slots (MAX_FILE_SLOTS of the firmware)
#define MATRIX_FX_STARS 12

// an image slot as the drive effect sees it
typedef struct {
  uint32_t sectors;       // 0: no image
  uint32_t lba;           // the last sector read or written
  uint8_t reads, writes;  // counters, they only go up (and wrap)
} matrix_fx_drive_t;

// what the effects take from the outside
typedef struct {
  uint8_t hour, minute, second;
  matrix_fx_drive_t drive[MATRIX_FX_DRIVES];
  const kg_mx_style_t *style; // the clock of karabas.ini [matrix]; NULL: the default look
} matrix_fx_input_t;

typedef struct {
  uint8_t effect;
  uint8_t color;    // KG_MX_* of the scanner, KG_MX_OFF = red
  uint32_t rnd;     // xorshift32 state
  uint16_t step;
  uint8_t fb[KG_MATRIX_H][KG_MATRIX_W];
  // fire: the heat of the cells, 0..15
  uint8_t heat[KG_MATRIX_H][KG_MATRIX_W];
  // life: the cells (bit x of row y), the generation before and the one before that
  uint16_t cells[KG_MATRIX_H], prev[KG_MATRIX_H], prev2[KG_MATRIX_H];
  uint8_t age[KG_MATRIX_H][KG_MATRIX_W];
  uint8_t still; // generations without change (or with period 2)
  // scanner
  int8_t pos, dir;
  // rain: the head row of the drop of a column (-1 none), slow drops move every second step
  int8_t drop[KG_MATRIX_W];
  uint16_t drop_slow;
  // stars: x in 1/16 pixels, the row, the layer (0 far .. 2 near)
  uint16_t star_x[MATRIX_FX_STARS];
  uint8_t star_y[MATRIX_FX_STARS], star_layer[MATRIX_FX_STARS];
  // pong: the ball (y in 1/16 pixels), the top rows of the paddles, steps to wait after a miss
  int8_t ball_x, ball_dx;
  int16_t ball_y, ball_dy;
  int8_t paddle[2];
  uint8_t serve_wait;
  // drive: the counters seen last, the steps the activity dot stays lit, its color
  uint8_t seen_reads[MATRIX_FX_DRIVES], seen_writes[MATRIX_FX_DRIVES];
  uint8_t led_steps[MATRIX_FX_DRIVES], led_color[MATRIX_FX_DRIVES];
} matrix_fx_t;

// the default frames (of 20 ms) per step of an effect
uint8_t matrix_fx_default_speed(uint8_t effect);

void matrix_fx_init(matrix_fx_t *fx, uint8_t effect, uint8_t color, uint32_t seed);

// one step: fx->fb holds the picture after it; in is used by the clock and the drive effect
void matrix_fx_step(matrix_fx_t *fx, const matrix_fx_input_t *in);

// life: one generation of the cells on a 16x8 torus (exposed for the tests)
void matrix_fx_life_next(const uint16_t *cells, uint16_t *out);

// a sine of a / 256 of a turn, -127..127
int8_t matrix_fx_sin8(uint8_t a);

#ifdef __cplusplus
}
#endif
