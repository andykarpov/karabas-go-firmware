#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "OSD.h"
#include "usb_hid_keys.h"
#include "SegaController.h"
#include "SdFat.h"
#include <algorithm>
#include <tuple>
#include "app_setup.h"
#include "app_core.h"
#include <SPI.h>
#include "sorts.h"
#include <GyverMenu.h>

GyverMenu setup_menu(32, 18);
uint8_t setup_day, setup_month, setup_year, setup_hour, setup_minute;

void app_setup_overlay() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();
  zxosd.header(core.build, core.id, HW_ID);
  print_time();
  zxosd.setPos(0,5);

  setup_day = zxrtc.getDay();
  setup_month = zxrtc.getMonth() - 1;
  setup_year = zxrtc.getYear();
  setup_hour = zxrtc.getHour();
  setup_minute = zxrtc.getMinute();

  setup_menu.setBackSign("< Return back");
  setup_menu.setFastCursor(false);

  setup_menu.onPrint([](const char* str, size_t len) {
      String s = String(str, len);
      zxosd.print(s);
      zxosd.update();
  });

  setup_menu.onCursor([](uint8_t row, bool choosen, bool active) -> uint8_t {
    zxosd.setPos(0, 5+row);
    uint8_t color = (choosen && !active)  ? OSD::COLOR_BLACK : 
                    (choosen && active)  ? OSD::COLOR_YELLOW_I : 
                    OSD::COLOR_WHITE;
    uint8_t bg_color = (choosen && !active) ? OSD::COLOR_WHITE : 
                       (choosen && active ) ? OSD::COLOR_FLASH : 
                       OSD::COLOR_BLACK;
    zxosd.setColor(color, bg_color);
    zxosd.update();
    return 0;
  });

  setup_menu.onBuild([](gm::Builder& b) {
    b.Label("Setup");
    b.Label("");

    b.Page(GM_NEXT, "Setup RTC", [](gm::Builder& b){
      b.Label("Setup RTC");
      b.Label("");
      b.ValueInt<uint8_t>("Year", &setup_year, 0, 99, 1, DEC, "", [](uint8_t) { zxrtc.setYear(setup_year); zxrtc.save(); });
      b.Select("Month", &setup_month, "January;February;March;April;May;June;July;August;September;October;November;December", [](uint8_t n, const char* str, uint8_t len) { zxrtc.setMonth(setup_month+1); zxrtc.save(); });
      b.ValueInt<uint8_t>("Day", &setup_day, 1, 31, 1, DEC, "", [](uint8_t) { zxrtc.setDay(setup_day); zxrtc.save(); });
      b.Label("");
      b.ValueInt<uint8_t>("Hour", &setup_hour, 0, 23, 1, DEC, "", [](uint8_t v) { zxrtc.setHour(v); zxrtc.save(); });
      b.ValueInt<uint8_t>("Minute", &setup_minute, 0, 59, 1, DEC, "", [](uint8_t v) { zxrtc.setMinute(v); zxrtc.save(); });
      b.Label("");
    });

    b.Page(GM_NEXT, "Options", [](gm::Builder& b) {
      b.Label("Setup Options");
      b.Label("");
      b.Label("TODO");
      b.Label("");
    });
  });

  setup_menu.refresh();

  // footer
  zxosd.line(23);
  zxosd.setPos(1,24); zxosd.print("Please use arrows to navigate");
  zxosd.setPos(1,25); zxosd.print("Press Esc to exit setup");
  zxosd.update();
}

void app_setup_on_keyboard() 
{
  if (usb_keyboard_report.keycode[0] == KEY_DOWN || (joyL & SC_BTN_DOWN) || (joyR & SC_BTN_DOWN)) {
    setup_menu.down();
  }

  if (usb_keyboard_report.keycode[0] == KEY_UP || (joyL & SC_BTN_UP) || (joyR & SC_BTN_UP)) {
    setup_menu.up();
  }

  // right
  if (usb_keyboard_report.keycode[0] == KEY_RIGHT || (joyL & SC_BTN_RIGHT) || (joyR & SC_BTN_RIGHT)) {
    setup_menu.right();
  }

  // left
  if (usb_keyboard_report.keycode[0] == KEY_LEFT || (joyL & SC_BTN_LEFT) || (joyR & SC_BTN_LEFT)) {
    setup_menu.left();
  }

  // enter
  if (usb_keyboard_report.keycode[0] == KEY_ENTER || (joyL & SC_BTN_A) || (joyR & SC_BTN_A) || (joyL & SC_BTN_B) || (joyR & SC_BTN_B)) {
    setup_menu.set();
  }

  // esc
  if (usb_keyboard_report.keycode[0] == KEY_ESC || (joyL & SC_BTN_C) || (joyR & SC_BTN_C)) {
    osd_state = osd_return_state;
  }

  setup_menu.refresh();
  zxosd.update();
}

void app_setup_on_time() {
  setup_day = zxrtc.getDay();
  setup_month = zxrtc.getMonth() - 1;
  setup_year = zxrtc.getYear();
  setup_hour = zxrtc.getHour();
  setup_minute = zxrtc.getMinute();
}

void app_esp_test() {
    uint8_t buffer[8192] = {0};

    if (wifi.createTCP("www.karabas.uk", 80)) {
        d_println("ESP8266 create tcp link to karabas.uk ok");
    } else {
        d_println("ESP8266 create tcp link to karabas.uk err");
    }

    const char *hello = "GET / HTTP/1.0\r\nHost: www.karabas.uk\r\nConnection: close\r\n\r\n";
    wifi.send((const uint8_t*)hello, strlen(hello));

    uint32_t len = 0;
    while (len = wifi.recv(buffer, sizeof(buffer), 10000)) {
      d_write(buffer, len);
    }

    if (wifi.releaseTCP()) {
      d_println("ESP8266 release tcp link ok");
    } else {
      d_println("ESP8266 release tcp link err");
    }
}
