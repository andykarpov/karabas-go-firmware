#pragma once

// The audio meters of the LED matrix (the core mode of a core that sends CMD_AUDIO_PEAKS_*),
// drawn into a 16x8 buffer of KG_MX_* colors in the style of karabas.ini [matrix].
// Pure C++, no Arduino dependencies.

#include <stdint.h>

#include "kg_matrix.h"
#include "matrix_style.h"

#define KG_METER_MAX 16        // the levels of a channel, 0..16
#define KG_METER_HOLD_FRAMES 50 // the peak dot stays for a second (frames of 20 ms)
#define KG_METER_FALL_FRAMES 3  // then falls one level every 3 frames

typedef struct {
  uint8_t peak[2]; // the held peak level of each channel
  uint8_t hold[2]; // the frames it stays
  uint8_t fall[2]; // the frames to its next fall
} kg_meter_t;

void kg_meter_init(kg_meter_t *m);
// the level 0..16 of a peak value of the core (a log scale, 200..20000)
uint8_t kg_meter_level(uint16_t v);
// one frame of decay of a peak value
uint16_t kg_meter_decay(uint16_t v, uint8_t decay);
// one frame: the levels of the left and the right channel into fb, the peak dots moved
void kg_meter_draw(kg_meter_t *m, const kg_mx_style_t *st, uint8_t left, uint8_t right,
  uint8_t fb[KG_MATRIX_H][KG_MATRIX_W]);
