#pragma once
// The optional SSD1306 OLED, 128x32 on I2C (0x3C), with its own charge pump: a frame buffer
// and 1-bit bitmaps (the layout of Adafruit GFX drawBitmap), sent whole with
// ssd1306_show() (about 12 ms at 400 kHz). The caller holds the I2C bus lock. Pure C.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_W 128
#define SSD1306_H 32

// found and started (the init sequence of Adafruit's SSD1306 library for 128x32), blank
bool ssd1306_begin(void);
void ssd1306_clear(void);
// rows of (w + 7) / 8 bytes, the left pixel in bit 7; set bits are lit, clipped to the screen
void ssd1306_bitmap(int x, int y, const uint8_t *bitmap, int w, int h);
bool ssd1306_show(void);

#ifdef __cplusplus
}
#endif
