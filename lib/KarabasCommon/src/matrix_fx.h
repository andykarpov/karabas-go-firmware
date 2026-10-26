#pragma once

// Effects of the LED matrix (make_core "matrix": {"mode": "effect"}), drawn into a 16x8
// buffer of KG_MX_* colors. Pure C++, no Arduino dependencies.

#include <stdint.h>

#include "kg_matrix.h"

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
} matrix_fx_t;

typedef struct {
  uint8_t hour, minute, second;
} matrix_fx_time_t;

// the default frames (of 20 ms) per step of an effect
uint8_t matrix_fx_default_speed(uint8_t effect);

void matrix_fx_init(matrix_fx_t *fx, uint8_t effect, uint8_t color, uint32_t seed);

// one step: fx->fb holds the picture after it; time is used by the clock only
void matrix_fx_step(matrix_fx_t *fx, const matrix_fx_time_t *time);

// life: one generation of the cells on a 16x8 torus (exposed for the tests)
void matrix_fx_life_next(const uint16_t *cells, uint16_t *out);
