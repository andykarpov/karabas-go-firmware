#pragma once

// LED matrix settings of a core: the 256-byte block at FILE_POS_MATRIX of the core file
// (make_core "matrix"). Pure C++, no Arduino dependencies.
//
// +0 mode, +1 color, +2 speed (frames of 20 ms per step, 0 = the mode's default),
// +3 repeat (scroll passes / animation loops, 0 = forever), +4 then (mode after the repeats),
// +5 effect, +6 brightness (0 = default, else 1..16), +7 frame count of the image (1..4),
// +16 text (64 chars, NUL padded), +80 four 16x8 frames of 32 bytes: the red plane, then
// the green one, 2 bytes per row, bit 7 of the first byte is x = 0.
// Core files made before this block have zeros there (mode core).

#include <stddef.h>
#include <stdint.h>

#define KG_MATRIX_BLOCK_SIZE 256
#define KG_MATRIX_TEXT_LEN 64
#define KG_MATRIX_FRAMES 4
#define KG_MATRIX_FRAME_SIZE 32
#define KG_MATRIX_W 16
#define KG_MATRIX_H 8

#define KG_MATRIX_POS_TEXT 16
#define KG_MATRIX_POS_FRAMES 80

// modes (and the "then" values)
#define KG_MATRIX_MODE_CORE 0   // the core draws / audio meters (the boot core: the default scroll)
#define KG_MATRIX_MODE_SCROLL 1
#define KG_MATRIX_MODE_EFFECT 2
#define KG_MATRIX_MODE_IMAGE 3
#define KG_MATRIX_MODE_BLANK 4
#define KG_MATRIX_MODE_COUNT 5

// pixel colors of the frames and effects
#define KG_MX_OFF 0
#define KG_MX_RED 1
#define KG_MX_GREEN 2
#define KG_MX_YELLOW 3

// text colors
#define KG_MATRIX_COLOR_DEFAULT 0 // yellow
#define KG_MATRIX_COLOR_RED 1
#define KG_MATRIX_COLOR_GREEN 2
#define KG_MATRIX_COLOR_YELLOW 3
#define KG_MATRIX_COLOR_RAINBOW 4  // by character: red, yellow, green
#define KG_MATRIX_COLOR_GRADIENT 5 // by column: green, yellow, red
#define KG_MATRIX_COLOR_COUNT 6

// effects
#define KG_MATRIX_FX_CLOCK 0
#define KG_MATRIX_FX_FIRE 1
#define KG_MATRIX_FX_LIFE 2
#define KG_MATRIX_FX_SCANNER 3
#define KG_MATRIX_FX_RAIN 4
#define KG_MATRIX_FX_PLASMA 5
#define KG_MATRIX_FX_STARS 6
#define KG_MATRIX_FX_PONG 7
#define KG_MATRIX_FX_DRIVE 8  // the activity of the mounted images (img_mount)
#define KG_MATRIX_FX_COUNT 9

typedef struct {
  uint8_t mode;
  uint8_t color;
  uint8_t speed;      // frames per step, 0 = the mode's default
  uint8_t repeat;     // 0 = forever
  uint8_t then;       // the mode after the repeats (never scroll / image)
  uint8_t effect;
  uint8_t brightness; // 0..15 (the HT16K33 level)
  uint8_t frame_count;
  char text[KG_MATRIX_TEXT_LEN + 1];
  uint8_t frames[KG_MATRIX_FRAMES][KG_MATRIX_FRAME_SIZE];
} kg_matrix_cfg_t;

#define KG_MATRIX_DEFAULT_BRIGHTNESS 4

// Parse the block; out-of-range values (0xFF of a never written block) fall back to the defaults.
void kg_matrix_parse(const uint8_t *block, kg_matrix_cfg_t *out);

// the color of the frame pixel (KG_MX_*)
uint8_t kg_matrix_frame_pixel(const uint8_t *frame, uint8_t x, uint8_t y);

// the color of a text pixel at screen column x, in character ch of the text (KG_MX_*)
uint8_t kg_matrix_text_color(uint8_t color, int x, int ch);

typedef struct {
  const char *name;
  const char *build;
  uint8_t hour, minute, day, month;
  uint16_t year;
} kg_matrix_vars_t;

// Expand {name}, {build}, {time} (HH:MM), {date} (DD.MM.YYYY) of the text; an unknown
// {...} stays as it is. Returns the length of out (truncated to size - 1).
size_t kg_matrix_expand(const char *text, const kg_matrix_vars_t *vars, char *out, size_t size);
