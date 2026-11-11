#include "config.h"
#include "types.h"
#include "main.h"
#include "ui/dialog.h"
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

// R on the About page of a kgo2 core: reset its settings, after a yes
static bool can_reset(void) {
  return osd_return_state == state_main && core.format == CORE_FORMAT_KGO2;
}
static bool confirm = false;

void app_about_overlay() {
  confirm = false;
  ui_screen("About");
  heading(8, "Karabas Go HW was designed by:");
  osd_set_pos(1, 10); osd_print("- andy.karpov");
  heading(12, "Special thanks to:");
  for (uint8_t i = 0; i < sizeof(thanks) / sizeof(thanks[0]); i++) {
    osd_set_pos(1, 14 + i); osd_print("- "); osd_print(thanks[i]);
  }
  ui_hint(can_reset() ? "R: reset the core settings" : NULL, "Press Esc to exit");
  osd_update();
}

void app_about_on_keyboard() 
{
  // esc, left
  uint8_t key = ui_nav_key();
  if (confirm) {
    if (key == KEY_ENTER) {
      dialog_box(DIALOG_BUSY, "Settings reset", "Restarting...");
      osd_state = osd_return_state;
      core_reset_settings();
    } else if (key == KEY_ESC) {
      app_about_overlay();
    }
    return;
  }
  if (key == KEY_R && can_reset()) {
    confirm = true;
    dialog_box(DIALOG_ASK, "Reset settings?", "Enter: yes");
    return;
  }
  if (key == KEY_ESC || (key == KEY_LEFT && !ui_repeat)) {
    osd_state = osd_return_state;
  }
  osd_update();
}
