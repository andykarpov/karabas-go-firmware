#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "osd.h"
#include "app_msc.h"
#include "app_core_browser.h"

// the screen of the USB mass storage mode: the boot core shows it while the PC has the SD card
// (msc_check_boot), nothing else runs until the card is ejected

#define MSC_LINES 5

static const char *const lines[MSC_LINES] = {
  "The SD card is connected to the",
  "PC as a USB drive.",
  "",
  "Eject it on the PC to return",
  "to the menu.",
};
static const char *const lines2[MSC_LINES] = {
  "The second SD card (FPGA slot)",
  "is connected to the PC as a USB",
  "drive.",
  "Eject it on the PC to return",
  "to the menu.",
};
static const char *const lines_failed[MSC_LINES] = {
  "The second SD card (FPGA slot)",
  "is not found.",
  "",
  "Back to the menu...",
  "",
};

static const char *const *msc_lines() {
  if (msc_failed()) return lines_failed;
  return msc_card() == 2 ? lines2 : lines;
}

// the graphic screen; on_time() redraws it for the clock
void app_msc_ft_draw() {
  ft.beginDisplayList();
  app_core_browser_ft_background(hw_setup);
  app_core_browser_ft_decor(hw_setup, true);
  uint16_t y = ft.height() / 2 - 80;
  ft.drawText(ft.width() / 2, y, 30, hw_setup.color_text, FT81x_OPT_CENTER, "USB mass storage");
  const char *const *l = msc_lines();
  for (uint8_t i = 0; i < MSC_LINES; i++) {
    ft.drawText(ft.width() / 2, y + 50 + i * 24, 28, hw_setup.color_text, FT81x_OPT_CENTER, l[i]);
  }
  ft.swapScreen();
}

void app_msc_overlay() {
  if (has_ft) {
    app_core_browser_ft_load();
    app_msc_ft_draw();
    return;
  }

  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_clear();
  osd_header(core.build, core.id, HW_ID);
  osd_set_pos(0, 5);
  osd_print("USB mass storage");
  osd_line(6);
  osd_set_color(OSD_COLOR_CYAN_I, OSD_COLOR_BLACK);
  const char *const *l = msc_lines();
  for (uint8_t i = 0; i < MSC_LINES; i++) {
    osd_set_pos(0, 9 + i); osd_print(l[i]);
  }
  osd_update();
}

void app_msc_on_keyboard() {
}
