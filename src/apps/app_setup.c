#include "config.h"
#include "types.h"
#include "main.h"
#include "osd.h"
#include "input/usb_hid_keys.h"
#include "input/sega.h"
#include "apps/app_setup.h"
#include "apps/app_core.h"
#include "ui/sorts.h"
// the Setup screen: a list of the setup apps; Wi-Fi and Settings only in the BOOT core
typedef struct {
  const char *label;
  enum osd_state_e state;
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
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_set_pos(0, SETUP_MENU_TOP);
  snprintf(line, sizeof(line), "%-32s", "Setup");
  osd_print(line);
  uint8_t n = setup_count();
  for (uint8_t pos = 0; pos < n; pos++) {
    bool sel = pos == setup_sel;
    osd_set_pos(0, SETUP_MENU_TOP + 2 + pos);
    osd_set_color(sel ? OSD_COLOR_BLACK : OSD_COLOR_WHITE, sel ? OSD_COLOR_WHITE : OSD_COLOR_BLACK);
    snprintf(line, sizeof(line), "%-32s", setup_item(pos)->label);
    osd_print(line);
  }
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
}

void app_setup_overlay() {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_clear();
  osd_header(core.build, core.id, HW_ID);
  print_time();

  if (setup_sel >= setup_count()) setup_sel = 0;
  setup_menu_draw();

  // footer
  osd_line(23);
  osd_set_pos(1,24); osd_print("Please use arrows to navigate");
  osd_set_pos(1,25); osd_print("Press Esc to exit setup");
  osd_update();
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
  osd_update();
}
