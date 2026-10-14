#include "main.h"
#define SSD1306_NO_SPLASH
#include "Adafruit_SSD1306.h"
#include "bitmaps.h"
#include "i2c_bus.h"

// Optional SSD1306 128x32 I2C OLED with a scrolling logo

static Adafruit_SSD1306 oled(128, 32, &Wire, -1);
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

void oled_tick() {
  if (has_oled && oled_timer.elapsed() >= 20) {
    oled_timer.reset();
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
}
