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
#include "app_about.h"
#include "app_core.h"
#include <SPI.h>
#include "sorts.h"
#include <GyverMenu.h>

void app_about_overlay() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();
  zxosd.header(core.build, core.id, HW_ID);
  print_time();
  zxosd.setPos(0,5);
  zxosd.print("About");
  zxosd.line(6);
  zxosd.setColor(OSD::COLOR_CYAN_I, OSD::COLOR_BLACK);
  zxosd.setPos(0,8); zxosd.print("Karabas Go HW was designed by:");
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.setPos(1,10); zxosd.print("- andy.karpov");
  zxosd.setColor(OSD::COLOR_CYAN_I, OSD::COLOR_BLACK);
  zxosd.setPos(0,12); zxosd.print("Special thanks to:");
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.setPos(1, 14); zxosd.print("- solegstar");
  zxosd.setPos(1, 15); zxosd.print("- nihirash");
  zxosd.setPos(1, 16); zxosd.print("- dr_max");
  zxosd.setPos(1, 17); zxosd.print("- tslabs");
  zxosd.setPos(1, 18); zxosd.print("- tank-uk");
  zxosd.setPos(1, 19); zxosd.print("- kalantaj");
  zxosd.setPos(1, 20); zxosd.print("- caasper911");
  zxosd.setPos(1, 21); zxosd.print("- xdemox");
  zxosd.setPos(1, 22); zxosd.print("- dumpkin");

  // footer
  zxosd.line(24);
  zxosd.setPos(1,25); zxosd.print("Press Esc to exit");
  zxosd.update();
}

void app_about_on_keyboard() 
{
  // esc
  if (usb_keyboard_report.keycode[0] == KEY_ESC || (joyL & SC_BTN_C) || (joyR & SC_BTN_C)) {
    osd_state = osd_return_state;
  }
  zxosd.update();
}
