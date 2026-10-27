#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The classic 5x7 font of Adafruit GFX (glcdfont.c, BSD license, see font5x7.c), 6 pixels a
// character: the text of the LED matrix scroll.

#define KG_FONT_ADVANCE 6

// a character column: bit 0 the top row
uint8_t kg_font_column(unsigned char c, uint8_t col);
// is the pixel x, y (y 0..7) of text drawn from x = pos lit? As GFX print(): no wrap, the
// characters from 176 on one further (its "classic" charset)
bool kg_text_pixel(const char *text, int pos, int x, int y);

#ifdef __cplusplus
}
#endif
