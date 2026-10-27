#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "OSD.h"
#include "usb_hid_keys.h"
#include "SegaController.h"
#include "ui/settings_items.h"
#include "app_settings.h"

// Setup > Settings (BOOT core only): the karabas.ini settings. Up / Down / Tab pick a setting,
// Left / Right / Enter change it. Save writes the changed values into karabas.ini and keeps the
// rest of the file. The look of the graphic menu is set in the graphic menu itself (app_ft_setup).
// Matrix effects opens the options of the clock and the audio meters on the same screen (Esc goes
// back); the matrix shows the plugin whose option has the focus.

// the settings of the screen, then the buttons
static const uint8_t ids[] = {
  SET_DEBUG, SET_DVI, SET_MATRIX, SET_MATRIX_BRIGHTNESS, SET_MX_EFFECTS,
  SET_AUTOLOAD, SET_TIMEOUT, SET_CORE,
  SET_FT, SET_VIDEO_MODE,
};
static const uint8_t rows[] = { 9, 10, 11, 12, 13, 15, 16, 17, 19, 20 };
// the Matrix effects page: settings_mx_ids
static const uint8_t mx_rows[] = { 9, 10, 11, 12, 15, 16, 17, 18 };
#define ITEMS (sizeof(ids) / sizeof(ids[0]))
#define I_SAVE ITEMS
#define I_CANCEL (ITEMS + 1)
#define I_COUNT (ITEMS + 2)

#define ROW_SYSTEM 8
#define ROW_AUTOLOAD 14
#define ROW_FT 18
#define ROW_CLOCK 8
#define ROW_METERS 14
#define ROW_BUTTONS 21
#define ROW_HINT 22
#define LABEL_X 2
#define VALUE_X 16 // '<', the value, '>'
#define VALUE_WIDTH 13
#define BTN_WIDTH 10

static setup_t s;     // the edited copy of hw_setup
static uint8_t focus;
static const char *error;
static bool mx_page;     // the Matrix effects page is shown
static uint8_t mx_focus; // its setting with the focus

static void draw_item(uint8_t id, uint8_t row, bool focused) {
  zxosd.setColor(focused ? OSD::COLOR_YELLOW_I : OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.setPos(LABEL_X, row); zxosd.print(id == SET_FT ? "Enabled" : settings[id].label);

  char value[VALUE_WIDTH + 1];
  settings_value_text(id, s, value, sizeof(value));
  if (settings[id].kind == SET_KIND_PAGE) strlcpy(value, "...", sizeof(value));
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.fill(VALUE_X, row, VALUE_X + VALUE_WIDTH + 1, row, ' ');
  if (focused && settings[id].kind != SET_KIND_PAGE) {
    zxosd.setPos(VALUE_X, row); zxosd.print("<");
    zxosd.setPos(VALUE_X + VALUE_WIDTH + 1, row); zxosd.print(">");
    zxosd.setColor(OSD::COLOR_BLACK, OSD::COLOR_YELLOW_I);
  } else if (focused) {
    zxosd.setColor(OSD::COLOR_BLACK, OSD::COLOR_YELLOW_I);
  } else {
    zxosd.setColor(OSD::COLOR_CYAN_I, OSD::COLOR_BLACK);
  }
  zxosd.setPos(VALUE_X + 1, row); zxosd.print(value);
}

static void section(uint8_t row, const char *title) {
  zxosd.setColor(OSD::COLOR_GREEN_I, OSD::COLOR_BLACK);
  zxosd.setPos(1, row); zxosd.print(title);
}

static void footer(const char *line1, const char *line2) {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.fill(0, 24, 31, 25, ' ');
  zxosd.setPos(1, 24); zxosd.print(line1);
  zxosd.setPos(1, 25); zxosd.print(line2);
}

// the sections and the footer of the main page or of the Matrix effects page
static void draw_frame() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.fill(0, 5, 31, 5, ' ');
  zxosd.fill(0, 7, 31, 22, ' ');
  zxosd.setPos(0, 5);
  if (mx_page) {
    zxosd.print("Settings > Matrix effects");
    section(ROW_CLOCK, "Clock");
    section(ROW_METERS, "Audio meters");
    footer("Up/Down: select, Left/Right:", "change, Esc: back");
  } else {
    zxosd.print("Settings");
    section(ROW_SYSTEM, "System");
    section(ROW_AUTOLOAD, "Autoload");
    section(ROW_FT, "Graphic menu (FT812)");
    footer("Up/Down: select, Left/Right:", "change, Esc: cancel");
  }
}

static void draw() {
  uint8_t hint_id = SET_COUNT;
  if (mx_page) {
    for (uint8_t i = 0; i < settings_mx_len; i++) draw_item(settings_mx_ids[i], mx_rows[i], mx_focus == i);
    hint_id = settings_mx_ids[mx_focus];
  } else {
    for (uint8_t i = 0; i < ITEMS; i++) draw_item(ids[i], rows[i], focus == i);
    ui_button(1, ROW_BUTTONS, BTN_WIDTH, "Save", focus == I_SAVE);
    ui_button(12, ROW_BUTTONS, BTN_WIDTH, "Cancel", focus == I_CANCEL);
    if (focus < ITEMS) hint_id = ids[focus];
  }
  settings_preview(mx_page ? hint_id : SET_COUNT);

  zxosd.setColor(error ? OSD::COLOR_RED_I : OSD::COLOR_GREY, OSD::COLOR_BLACK);
  zxosd.fill(0, ROW_HINT, 31, ROW_HINT, ' ');
  zxosd.setPos(1, ROW_HINT);
  if (error) zxosd.print(error);
  else if (hint_id < SET_COUNT) zxosd.print(settings[hint_id].hint);
  zxosd.update();
}

static void show_page(bool mx) {
  mx_page = mx;
  mx_focus = 0;
  draw_frame();
  draw();
}

void app_settings_overlay() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();
  zxosd.header(core.build, core.id, HW_ID);
  print_time();
  zxosd.line(6);
  zxosd.line(23);

  s = hw_setup;
  settings_begin(s);
  focus = 0;
  error = nullptr;
  show_page(false);
}

static void mx_on_keyboard(bool up, bool down, bool left, bool right, bool enter, bool esc, bool back) {
  if (esc || back) { show_page(false); return; }
  if (up) mx_focus = (mx_focus + settings_mx_len - 1) % settings_mx_len;
  else if (down) mx_focus = (mx_focus + 1) % settings_mx_len;
  else if (left || right || enter) settings_change(settings_mx_ids[mx_focus], s, left ? -1 : 1);
  else return;
  draw();
}

void app_settings_on_keyboard() {
  uint8_t key = usb_keyboard_report.keycode[0];
  bool up = key == KEY_UP || (joyL & SC_BTN_UP) || (joyR & SC_BTN_UP);
  bool down = key == KEY_DOWN || key == KEY_TAB || (joyL & SC_BTN_DOWN) || (joyR & SC_BTN_DOWN);
  bool left = key == KEY_LEFT || (joyL & SC_BTN_LEFT) || (joyR & SC_BTN_LEFT);
  bool right = key == KEY_RIGHT || (joyL & SC_BTN_RIGHT) || (joyR & SC_BTN_RIGHT);
  bool enter = key == KEY_ENTER || key == KEY_KPENTER || (joyL & SC_BTN_A) || (joyR & SC_BTN_A) || (joyL & SC_BTN_B) || (joyR & SC_BTN_B);
  bool esc = key == KEY_ESC || ((joyL | joyR) & UI_JOY_ESC);
  bool button = focus == I_SAVE || focus == I_CANCEL;

  if (mx_page) {
    mx_on_keyboard(up, down, left, right, enter, esc, key == KEY_BACKSPACE);
    return;
  }

  if (esc) {
    settings_end();
    osd_state = state_setup;
    return;
  }
  if (up) {
    focus = (focus + I_COUNT - 1) % I_COUNT;
  } else if (down) {
    focus = (focus + 1) % I_COUNT;
  } else if (button && (left || right)) {
    focus = focus == I_SAVE ? I_CANCEL : I_SAVE;
  } else if (!button && settings[ids[focus]].kind == SET_KIND_PAGE) {
    if (enter || right) { show_page(true); return; }
    return;
  } else if (!button && (left || right || enter)) {
    settings_change(ids[focus], s, left ? -1 : 1);
  } else if (enter && focus == I_SAVE) {
    if (save_setup(s)) { osd_state = state_setup; return; }
    error = "Can't save karabas.ini";
    draw();
    return;
  } else if (enter && focus == I_CANCEL) {
    settings_end();
    osd_state = state_setup;
    return;
  } else {
    return;
  }
  error = nullptr;
  draw();
}
