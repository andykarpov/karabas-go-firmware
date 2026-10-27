#include "config.h"
#include "types.h"
#include "main.h"
#include "osd.h"
#include "usb_hid_keys.h"
#include "input/sega.h"
#include "app_about.h"
#include "app_core.h"
#include "sorts.h"

void app_about_overlay() {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_clear();
  osd_header(core.build, core.id, HW_ID);
  print_time();
  osd_set_pos(0,5);
  osd_print("About");
  osd_line(6);
  osd_set_color(OSD_COLOR_CYAN_I, OSD_COLOR_BLACK);
  osd_set_pos(0,8); osd_print("Karabas Go HW was designed by:");
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_set_pos(1,10); osd_print("- andy.karpov");
  osd_set_color(OSD_COLOR_CYAN_I, OSD_COLOR_BLACK);
  osd_set_pos(0,12); osd_print("Special thanks to:");
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_set_pos(1, 14); osd_print("- solegstar");
  osd_set_pos(1, 15); osd_print("- nihirash");
  osd_set_pos(1, 16); osd_print("- dr_max");
  osd_set_pos(1, 17); osd_print("- tslabs");
  osd_set_pos(1, 18); osd_print("- tank-uk");
  osd_set_pos(1, 19); osd_print("- kalantaj");
  osd_set_pos(1, 20); osd_print("- caasper911");
  osd_set_pos(1, 21); osd_print("- xdemox");
  osd_set_pos(1, 22); osd_print("- dumpkin");

  // footer
  osd_line(24);
  osd_set_pos(1,25); osd_print("Press Esc to exit");
  osd_update();
}

void app_about_on_keyboard() 
{
  // esc, left
  bool left = usb_keyboard_report.keycode[0] == KEY_LEFT || ((joyL | joyR) & SC_BTN_LEFT);
  if (usb_keyboard_report.keycode[0] == KEY_ESC || ((joyL | joyR) & UI_JOY_ESC) || (left && !ui_repeat)) {
    osd_state = osd_return_state;
  }
  osd_update();
}
