#include "config.h"
#include "types.h"
#include "main.h"
#include "osd.h"
#include "input/usb_hid_keys.h"
#include "input/sega.h"
#include "apps/app_setup.h"
#include "apps/app_core.h"
#include "ui/sorts.h"
#include "ui/ui_draw.h"
#include "ui/osd2.h"
#include "ui/ui2.h"
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

uint8_t app_setup_count(void) { return setup_count(); }
const char *app_setup_label(uint8_t pos) { const setup_item_t *i = setup_item(pos); return i ? i->label : ""; }
uint8_t app_setup_state(uint8_t pos) { const setup_item_t *i = setup_item(pos); return i ? i->state : state_setup; }

static bool from_menu = false;
void app_setup_opened_from_menu(bool m) { from_menu = m; }
uint8_t app_setup_back(void) { return from_menu ? state_main : state_setup; }

static void setup_menu_draw() {
  uint8_t n = setup_count();
  if (osd2_active()) {
    for (uint8_t pos = 0; pos < n && pos < UI2_ROWS; pos++) ui2_text(pos, setup_item(pos)->label, pos == setup_sel ? UI2_SEL : 0);
    ui2_done();
    return;
  }
  for (uint8_t pos = 0; pos < n; pos++) {
    ui_row(0, UI_ROW_BODY + pos, setup_item(pos)->label, pos == setup_sel);
  }
}

void app_setup_overlay() {
  if (osd2_active()) {
    ui2_screen("Setup");
    if (setup_sel >= setup_count()) setup_sel = 0;
    ui2_hint("Up / Down: choose, Enter: open", "Esc: back");
    setup_menu_draw();
    return;
  }
  ui_screen("Setup");
  if (setup_sel >= setup_count()) setup_sel = 0;
  setup_menu_draw();
  ui_hint("Please use arrows to navigate", "Press Esc to exit setup");
  osd_update();
}

void app_setup_on_keyboard() {
  uint8_t key = ui_nav_key();
  uint16_t sel = setup_sel;
  // up / down (no pages: Left goes back)
  if (key == KEY_UP || key == KEY_DOWN) ui_list_key(&sel, setup_count(), 1, key);
  setup_sel = sel;

  // enter (right does nothing: the items are all buttons)
  if (key == KEY_ENTER) {
    const setup_item_t *item = setup_item(setup_sel);
    if (item) { from_menu = false; osd_state = item->state; }
  }

  // esc, left (the menu has only buttons)
  if (key == KEY_ESC || (key == KEY_LEFT && !ui_repeat)) {
    osd_state = osd_return_state;
  }

  setup_menu_draw();
  osd_update();
}
