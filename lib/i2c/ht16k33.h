#pragma once
// The LED matrix add-on (docs/matrix.md): two HT16K33 (0x70 left, 0x71 right), each driving a
// bi-color 8x8 matrix, as one 16x8 display. The pixels are drawn into a buffer and sent with
// ht16k33_show(). The caller holds the I2C bus lock. Pure C.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HT16K33_W 16
#define HT16K33_H 8

// the colors (the values of Adafruit's LED Backpack, also those of CMD_MATRIX_PIXEL)
#define HT16K33_OFF 0
#define HT16K33_RED 1
#define HT16K33_YELLOW 2
#define HT16K33_GREEN 3

// both chips started (oscillator on, blank, display on, full brightness); false if one
// did not answer
bool ht16k33_begin(void);
void ht16k33_clear(void);
void ht16k33_pixel(int x, int y, uint8_t color);
// one color plane (red or green) of row y of a chip: bit n is the pixel x = chip * 8 + n
void ht16k33_row(uint8_t chip, uint8_t y, bool red, uint8_t bits);
bool ht16k33_show(void);
void ht16k33_brightness(uint8_t level); // 0..15
// the key scan of the first chip (the buttons of the add-on), 8 bits of row 0
uint8_t ht16k33_keys(void);

#ifdef __cplusplus
}
#endif
