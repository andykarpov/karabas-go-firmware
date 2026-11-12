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
#include "ui/osd2.h"
#include "ui/ui2.h"

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

// the bitmap OSD (link v2): the logo, the author, the thanks in the small font, the builds
static void about2(void) {
  ui2_screen("About");
  ui2_logo((OSD2_W - 56) / 2, 18);
  u8g2_SetFontPosTop(&osd2);
  u8g2_SetFont(&osd2, u8g2_font_6x12_t_cyrillic);
  const char *by = "Hardware: andy.karpov";
  u8g2_DrawUTF8(&osd2, (OSD2_W - u8g2_GetUTF8Width(&osd2, by)) / 2, 40, by);
  u8g2_SetFont(&osd2, u8g2_font_5x8_t_cyrillic);
  const char *th = "Special thanks to";
  u8g2_DrawUTF8(&osd2, (OSD2_W - u8g2_GetUTF8Width(&osd2, th)) / 2, 58, th);
  // the names, centred lines of at most 46 characters
  char line[64];
  int y = 70;
  line[0] = '\0';
  for (uint8_t i = 0; i <= sizeof(thanks) / sizeof(thanks[0]); i++) {
    bool last = i == sizeof(thanks) / sizeof(thanks[0]);
    if (last || strlen(line) + strlen(thanks[i]) + 2 > 46) {
      u8g2_DrawUTF8(&osd2, (OSD2_W - u8g2_GetUTF8Width(&osd2, line)) / 2, y, line);
      y += 9;
      line[0] = '\0';
      if (last) break;
    }
    if (line[0]) strlcat(line, ", ", sizeof(line));
    strlcat(line, thanks[i], sizeof(line));
  }
  char b[48];
  snprintf(b, sizeof(b), "MCU %s   FPGA %s%s", STRINGIZE_VALUE_OF(BUILD_VER), core.build, can_reset() ? "   R: reset" : "");
  u8g2_DrawUTF8(&osd2, (OSD2_W - u8g2_GetUTF8Width(&osd2, b)) / 2, 118, b);
  ui2_done();
}

void app_about_overlay() {
  confirm = false;
  if (osd2_active()) { about2(); return; }
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
