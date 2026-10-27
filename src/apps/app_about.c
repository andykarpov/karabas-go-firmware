#include "config.h"
#include "types.h"
#include "main.h"
#include "osd.h"
#include "input/usb_hid_keys.h"
#include "input/sega.h"
#include "apps/app_about.h"
#include "apps/app_core.h"
#include "ui/ui_draw.h"

static const char *const thanks[] = {
  "solegstar", "nihirash", "dr_max", "tslabs", "tank-uk", "kalantaj", "caasper911", "xdemox", "dumpkin",
};

static void heading(uint8_t y, const char *text) {
  osd_set_color(OSD_COLOR_CYAN_I, OSD_COLOR_BLACK);
  osd_set_pos(0, y); osd_print(text);
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
}

void app_about_overlay() {
  ui_screen("About");
  heading(8, "Karabas Go HW was designed by:");
  osd_set_pos(1, 10); osd_print("- andy.karpov");
  heading(12, "Special thanks to:");
  for (uint8_t i = 0; i < sizeof(thanks) / sizeof(thanks[0]); i++) {
    osd_set_pos(1, 14 + i); osd_print("- "); osd_print(thanks[i]);
  }
  ui_hint(NULL, "Press Esc to exit");
  osd_update();
}

void app_about_on_keyboard() 
{
  // esc, left
  uint8_t key = ui_nav_key();
  if (key == KEY_ESC || (key == KEY_LEFT && !ui_repeat)) {
    osd_state = osd_return_state;
  }
  osd_update();
}
