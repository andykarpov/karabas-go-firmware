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
// the Setup screen: a list of the setup apps; Wi-Fi and Settings only in the BOOT core
typedef struct {
  const char *label;
  osd_state_e state;
  bool boot_only;
} setup_item_t;

static const setup_item_t setup_items[] = {
  { "Setup RTC",       state_rtc,        false },
  { "Setup Wi-Fi",     state_wifi,       true },
  { "Settings",        state_settings,   true },
  { "Map USB gamepad", state_pad_mapper, false },
};
#define SETUP_ITEMS (sizeof(setup_items) / sizeof(setup_items[0]))
#define SETUP_MENU_TOP 5 // the "Setup" title; the items start 2 rows below

static uint8_t setup_sel = 0; // index among the shown items, kept while the OSD is open

static bool setup_item_shown(uint8_t i) {
  return !setup_items[i].boot_only || core.type == CORE_TYPE_BOOT;
}

static uint8_t setup_count() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < SETUP_ITEMS; i++) if (setup_item_shown(i)) n++;
  return n;
}

// the item at the shown position pos
static const setup_item_t *setup_item(uint8_t pos) {
  for (uint8_t i = 0; i < SETUP_ITEMS; i++) {
    if (!setup_item_shown(i)) continue;
    if (pos-- == 0) return &setup_items[i];
  }
  return NULL;
}

static void setup_menu_draw() {
  char line[33];
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.setPos(0, SETUP_MENU_TOP);
  snprintf(line, sizeof(line), "%-32s", "Setup");
  zxosd.print(line);
  uint8_t n = setup_count();
  for (uint8_t pos = 0; pos < n; pos++) {
    bool sel = pos == setup_sel;
    zxosd.setPos(0, SETUP_MENU_TOP + 2 + pos);
    zxosd.setColor(sel ? OSD::COLOR_BLACK : OSD::COLOR_WHITE, sel ? OSD::COLOR_WHITE : OSD::COLOR_BLACK);
    snprintf(line, sizeof(line), "%-32s", setup_item(pos)->label);
    zxosd.print(line);
  }
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
}

void app_setup_overlay() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();
  zxosd.header(core.build, core.id, HW_ID);
  print_time();

  if (setup_sel >= setup_count()) setup_sel = 0;
  setup_menu_draw();

  // footer
  zxosd.line(23);
  zxosd.setPos(1,24); zxosd.print("Please use arrows to navigate");
  zxosd.setPos(1,25); zxosd.print("Press Esc to exit setup");
  zxosd.update();
}

void app_setup_on_keyboard() 
{
  if (usb_keyboard_report.keycode[0] == KEY_DOWN || (joyL & SC_BTN_DOWN) || (joyR & SC_BTN_DOWN)) {
    if (setup_sel + 1 < setup_count()) setup_sel++;
  }

  if (usb_keyboard_report.keycode[0] == KEY_UP || (joyL & SC_BTN_UP) || (joyR & SC_BTN_UP)) {
    if (setup_sel > 0) setup_sel--;
  }

  // enter (right does nothing: the items are all buttons)
  if (usb_keyboard_report.keycode[0] == KEY_ENTER || (joyL & SC_BTN_A) || (joyR & SC_BTN_A) || (joyL & SC_BTN_B) || (joyR & SC_BTN_B)) {
    const setup_item_t *item = setup_item(setup_sel);
    if (item) osd_state = item->state;
  }

  // esc, left (the menu has only buttons)
  bool left = usb_keyboard_report.keycode[0] == KEY_LEFT || ((joyL | joyR) & SC_BTN_LEFT);
  if (usb_keyboard_report.keycode[0] == KEY_ESC || ((joyL | joyR) & UI_JOY_ESC) || (left && !ui_repeat)) {
    osd_state = osd_return_state;
  }

  setup_menu_draw();
  zxosd.update();
}
