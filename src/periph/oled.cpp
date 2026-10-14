#include "main.h"
#define SSD1306_NO_SPLASH
#include "Adafruit_SSD1306.h"
#include "bitmaps.h"
#include "i2c_bus.h"

// Optional SSD1306 128x32 I2C OLED with a scrolling logo

// the library switches the bus clock around each transfer (default: 400 kHz during, 100 kHz
// after); keep the global 400 kHz set in setup()
static Adafruit_SSD1306 oled(128, 32, &Wire, -1, 400000UL, 400000UL);
static bool has_oled = false;
static int oled_scrollpos = -128;
static ElapsedTimer oled_timer;

void oled_begin() {
  I2cGuard i2c;
  has_oled = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (has_oled) {
    d_println("Found SSD1306 OLED display");
    oled.clearDisplay();
    oled.drawBitmap(8, 0, logoBw, 112, 32, WHITE); // karabas go logo 112x32 bw
    oled.display(); // show default adafruit buffer
  } else {
    d_println("SSD1306 OLED display was not found");
  }
}

bool oled_present() {
  return has_oled;
}

// one 20 ms frame of the scrolling logo
void oled_frame() {
  if (!has_oled) return;
  I2cGuard i2c;
  if (oled_scrollpos >= -112) {
    oled_scrollpos--;
  } else {
    oled_scrollpos = 128;
  }
  oled.clearDisplay();
  oled.drawBitmap(oled_scrollpos, 0, logoBw, 112, 32, WHITE); // karabas go logo 112x32 bw
  oled.display(); // show default adafruit buffer
}

// plain build: called from the main loop
void oled_tick() {
  if (has_oled && oled_timer.elapsed() >= 20) {
    oled_timer.reset();
    oled_frame();
  }
}
