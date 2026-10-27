#include "ssd1306.h"
#include <string.h>
#include "i2c_dev.h"

#define SSD1306_ADDR 0x3C
#define SSD1306_CMD 0x00  // control byte: a command list follows
#define SSD1306_DATA 0x40 // control byte: display RAM data follows

// the control byte, then the pages of 128 columns (bit 0 the top row of a page)
static uint8_t frame[1 + SSD1306_W * SSD1306_H / 8];

static bool commands(const uint8_t *cmds, size_t len) {
  return i2c_dev_write_reg(SSD1306_ADDR, SSD1306_CMD, cmds, len);
}

bool ssd1306_begin(void) {
  static const uint8_t init[] = {
    0xAE,             // display off
    0xD5, 0x80,       // clock divide ratio
    0xA8, SSD1306_H - 1, // multiplex
    0xD3, 0x00,       // display offset
    0x40,             // start line 0
    0x8D, 0x14,       // charge pump on (internal VCC)
    0x20, 0x00,       // horizontal addressing
    0xA1,             // segment remap
    0xC8,             // COM scan decrementing
    0xDA, 0x02,       // COM pins (128x32)
    0x81, 0x8F,       // contrast
    0xD9, 0xF1,       // precharge (internal VCC)
    0xDB, 0x40,       // VCOMH deselect level
    0xA4,             // display follows RAM
    0xA6,             // normal, not inverted
    0x2E,             // no scrolling
    0xAF,             // display on
  };
  if (!i2c_dev_probe(SSD1306_ADDR)) return false;
  ssd1306_clear();
  return commands(init, sizeof(init));
}

void ssd1306_clear(void) {
  memset(frame, 0, sizeof(frame));
  frame[0] = SSD1306_DATA;
}

static void pixel(int x, int y) {
  if (x < 0 || x >= SSD1306_W || y < 0 || y >= SSD1306_H) return;
  frame[1 + x + (y / 8) * SSD1306_W] |= 1 << (y & 7);
}

void ssd1306_bitmap(int x, int y, const uint8_t *bitmap, int w, int h) {
  int bytes = (w + 7) / 8;
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      if (bitmap[j * bytes + i / 8] & (0x80 >> (i & 7))) pixel(x + i, y + j);
    }
  }
}

bool ssd1306_show(void) {
  static const uint8_t window[] = {
    0x22, 0x00, 0xFF,          // pages: all
    0x21, 0x00, SSD1306_W - 1, // columns: all
  };
  return commands(window, sizeof(window)) && i2c_dev_write(SSD1306_ADDR, frame, sizeof(frame));
}
