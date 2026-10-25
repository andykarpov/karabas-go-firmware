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

void app_setup_overlay() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();
  zxosd.header(core.build, core.id, HW_ID);
  print_time();
  zxosd.setPos(0,5);

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

    b.Button("Setup RTC", []() { osd_state = state_rtc; });

    if (core.type == CORE_TYPE_BOOT) {
      b.Button("Setup Wi-Fi", []() { osd_state = state_wifi; });
      b.Button("Settings", []() { osd_state = state_settings; });
    }

    b.Button("Map USB gamepad", []() { osd_state = state_pad_mapper; });
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
