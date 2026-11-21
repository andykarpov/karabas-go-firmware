#include "config.h"
#include "types.h"
#include "main.h"
#include "osd.h"
#include "apps/app_boot.h"
#include "ui/ui_draw.h"
#include "ui/osd2.h"
#include "ui/ui2.h"

static const char *const lines[] = {
  "This boot.kg is too old",
  "for the firmware " STRINGIZE_VALUE_OF(BUILD_VER),
  "",
  "Put the new boot.kg on the SD card:",
  "press BTN2, the card becomes",
  "a USB drive of the PC",
};
#define LINES (sizeof(lines) / sizeof(lines[0]))

void app_boot_overlay() {
  if (osd2_active()) {
    ui2_screen_note("Karabas", NULL, false);
    ui2_center(LINES, 0);
    for (uint8_t i = 0; i < LINES; i++) ui2_text_center(i, lines[i]);
    ui2_done();
    return;
  }
  ui_screen("Karabas");
  osd_set_color(OSD_COLOR_YELLOW_I, OSD_COLOR_BLACK);
  for (uint8_t i = 0; i < LINES; i++) { osd_set_pos(0, 8 + i); osd_print(lines[i]); }
  osd_update();
}

void app_boot_on_keyboard() {
}
