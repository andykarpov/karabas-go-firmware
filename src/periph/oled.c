#include "main.h"
#include "ssd1306.h"
#include "bitmaps.h"
#include "i2c_bus.h"

// Optional SSD1306 128x32 I2C OLED with a scrolling logo (lib/i2c ssd1306)

static bool has_oled = false;
static int oled_scrollpos = -128;

void oled_begin() {
  i2c_bus_lock();
  has_oled = ssd1306_begin();
  if (has_oled) {
    d_println("Found SSD1306 OLED display");
    ssd1306_bitmap(8, 0, logoBw, 112, 32); // karabas go logo 112x32 bw
    ssd1306_show();
  } else {
    d_println("SSD1306 OLED display was not found");
  }
  i2c_bus_unlock();
}

bool oled_present() {
  return has_oled;
}

// one 20 ms frame of the scrolling logo
void oled_frame() {
  if (!has_oled) return;
  i2c_bus_lock();
  if (oled_scrollpos >= -112) {
    oled_scrollpos--;
  } else {
    oled_scrollpos = 128;
  }
  ssd1306_clear();
  ssd1306_bitmap(oled_scrollpos, 0, logoBw, 112, 32); // karabas go logo 112x32 bw
  ssd1306_show();
  i2c_bus_unlock();
}
