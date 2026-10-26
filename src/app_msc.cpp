#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "OSD.h"
#include "app_msc.h"
#include "app_core_browser.h"

// the screen of the USB mass storage mode: the boot core shows it while the PC has the SD card
// (msc_check_boot), nothing else runs until the card is ejected

static const char *const lines[] = {
  "The SD card is connected to the PC",
  "as a USB drive.",
  "",
  "Eject it on the PC to return",
  "to the menu.",
};
static const uint8_t lines_len = sizeof(lines) / sizeof(lines[0]);

void app_msc_overlay() {
  if (has_ft) {
    app_core_browser_ft_load();
    ft.beginDisplayList();
    app_core_browser_ft_background(hw_setup);
    app_core_browser_ft_decor(hw_setup, true);
    uint16_t y = ft.height() / 2 - 80;
    ft.drawText(ft.width() / 2, y, 30, hw_setup.color_text, FT81x_OPT_CENTER, "USB mass storage");
    for (uint8_t i = 0; i < lines_len; i++) {
      ft.drawText(ft.width() / 2, y + 50 + i * 24, 28, hw_setup.color_text, FT81x_OPT_CENTER, lines[i]);
    }
    ft.swapScreen();
    return;
  }

  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();
  zxosd.header(core.build, core.id, HW_ID);
  zxosd.setPos(0, 5);
  zxosd.print("USB mass storage");
  zxosd.line(6);
  zxosd.setColor(OSD::COLOR_CYAN_I, OSD::COLOR_BLACK);
  for (uint8_t i = 0; i < lines_len; i++) {
    zxosd.setPos(0, 9 + i); zxosd.print(lines[i]);
  }
  zxosd.update();
}

void app_msc_on_keyboard() {
}
