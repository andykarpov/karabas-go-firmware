#include "ht16k33.h"
#include <string.h>
#include "i2c_dev.h"

#define HT16K33_ADDR 0x70
#define HT16K33_CHIPS 2
#define HT16K33_OSC_ON 0x21
#define HT16K33_DISPLAY_ON 0x81 // no blink
#define HT16K33_BRIGHTNESS 0xE0
#define HT16K33_KEYS 0x40

// per chip 8 rows: bits 0..7 green, 8..15 red
static uint16_t rows[HT16K33_CHIPS][8];

static bool command(uint8_t chip, uint8_t cmd) {
  return i2c_dev_write(HT16K33_ADDR + chip, &cmd, 1);
}

static bool show_chip(uint8_t chip) {
  uint8_t buf[17];
  buf[0] = 0x00; // display RAM from address 0
  for (uint8_t i = 0; i < 8; i++) {
    buf[1 + 2 * i] = rows[chip][i] & 0xFF;
    buf[2 + 2 * i] = rows[chip][i] >> 8;
  }
  return i2c_dev_write(HT16K33_ADDR + chip, buf, sizeof(buf));
}

bool ht16k33_begin(void) {
  bool all = true;
  memset(rows, 0, sizeof(rows));
  for (uint8_t chip = 0; chip < HT16K33_CHIPS; chip++) {
    if (!i2c_dev_probe(HT16K33_ADDR + chip)) {
      all = false;
      continue;
    }
    // the display RAM powers up with random values: blank it before the display goes on
    command(chip, HT16K33_OSC_ON);
    show_chip(chip);
    command(chip, HT16K33_DISPLAY_ON);
    command(chip, HT16K33_BRIGHTNESS | 15);
  }
  return all;
}

void ht16k33_clear(void) {
  memset(rows, 0, sizeof(rows));
}

void ht16k33_pixel(int x, int y, uint8_t color) {
  if (x < 0 || x >= HT16K33_W || y < 0 || y >= HT16K33_H) return;
  uint16_t *row = &rows[x / 8][y];
  uint16_t green = 1 << (x % 8), red = 1 << (x % 8 + 8);
  switch (color) {
    case HT16K33_GREEN: *row = (*row | green) & ~red; break;
    case HT16K33_RED: *row = (*row | red) & ~green; break;
    case HT16K33_YELLOW: *row |= green | red; break;
    case HT16K33_OFF: *row &= ~(green | red); break;
    default: break;
  }
}

bool ht16k33_show(void) {
  bool ok = true;
  for (uint8_t chip = 0; chip < HT16K33_CHIPS; chip++) {
    if (!show_chip(chip)) ok = false;
  }
  return ok;
}

void ht16k33_brightness(uint8_t level) {
  if (level > 15) level = 15;
  for (uint8_t chip = 0; chip < HT16K33_CHIPS; chip++) {
    command(chip, HT16K33_BRIGHTNESS | level);
  }
}

uint8_t ht16k33_keys(void) {
  uint8_t keys[2];
  if (!i2c_dev_read_reg(HT16K33_ADDR, HT16K33_KEYS, keys, sizeof(keys))) return 0;
  return keys[0];
}
