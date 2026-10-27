#include "config.h"
#include "types.h"
#include "main.h"
#include "osd.h"
#include "input/usb_hid_keys.h"
#include "input/sega.h"
#include "ui/settings_items.h"
#include "apps/app_settings.h"

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
  osd_set_color(focused ? OSD_COLOR_YELLOW_I : OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_set_pos(LABEL_X, row); osd_print(id == SET_FT ? "Enabled" : settings[id].label);

  char value[VALUE_WIDTH + 1];
  settings_value_text(id, &s, value, sizeof(value));
  if (settings[id].kind == SET_KIND_PAGE) strlcpy(value, "...", sizeof(value));
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_fill(VALUE_X, row, VALUE_X + VALUE_WIDTH + 1, row, ' ');
  if (focused && settings[id].kind != SET_KIND_PAGE) {
    osd_set_pos(VALUE_X, row); osd_print("<");
    osd_set_pos(VALUE_X + VALUE_WIDTH + 1, row); osd_print(">");
    osd_set_color(OSD_COLOR_BLACK, OSD_COLOR_YELLOW_I);
  } else if (focused) {
    osd_set_color(OSD_COLOR_BLACK, OSD_COLOR_YELLOW_I);
  } else {
    osd_set_color(OSD_COLOR_CYAN_I, OSD_COLOR_BLACK);
  }
  osd_set_pos(VALUE_X + 1, row); osd_print(value);
}

static void section(uint8_t row, const char *title) {
  osd_set_color(OSD_COLOR_GREEN_I, OSD_COLOR_BLACK);
  osd_set_pos(1, row); osd_print(title);
}

static void footer(const char *line1, const char *line2) {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_fill(0, 24, 31, 25, ' ');
  osd_set_pos(1, 24); osd_print(line1);
  osd_set_pos(1, 25); osd_print(line2);
}

// the sections and the footer of the main page or of the Matrix effects page
static void draw_frame() {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_fill(0, 5, 31, 5, ' ');
  osd_fill(0, 7, 31, 22, ' ');
  osd_set_pos(0, 5);
  if (mx_page) {
    osd_print("Settings > Matrix effects");
    section(ROW_CLOCK, "Clock");
    section(ROW_METERS, "Audio meters");
    footer("Up/Down: select, Left/Right:", "change, Esc: back");
  } else {
    osd_print("Settings");
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

  osd_set_color(error ? OSD_COLOR_RED_I : OSD_COLOR_GREY, OSD_COLOR_BLACK);
  osd_fill(0, ROW_HINT, 31, ROW_HINT, ' ');
  osd_set_pos(1, ROW_HINT);
  if (error) osd_print(error);
  else if (hint_id < SET_COUNT) osd_print(settings[hint_id].hint);
  osd_update();
}

static void show_page(bool mx) {
  mx_page = mx;
  mx_focus = 0;
  draw_frame();
  draw();
}

void app_settings_overlay() {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_clear();
  osd_header(core.build, core.id, HW_ID);
  print_time();
  osd_line(6);
  osd_line(23);

  s = hw_setup;
  settings_begin(&s);
  focus = 0;
  error = NULL;
  show_page(false);
}

static void mx_on_keyboard(bool up, bool down, bool left, bool right, bool enter, bool esc, bool back) {
  if (esc || back) { show_page(false); return; }
  if (up) mx_focus = (mx_focus + settings_mx_len - 1) % settings_mx_len;
  else if (down) mx_focus = (mx_focus + 1) % settings_mx_len;
  else if (left || right || enter) settings_change(settings_mx_ids[mx_focus], &s, left ? -1 : 1);
  else return;
  draw();
}

void app_settings_on_keyboard() {
  uint8_t key = ui_nav_key();
  bool up = key == KEY_UP;
  bool down = key == KEY_DOWN || key == KEY_TAB;
  bool left = key == KEY_LEFT;
  bool right = key == KEY_RIGHT;
  bool enter = key == KEY_ENTER;
  bool esc = key == KEY_ESC;
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
    settings_change(ids[focus], &s, left ? -1 : 1);
  } else if (enter && focus == I_SAVE) {
    if (save_setup(&s)) { osd_state = state_setup; return; }
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
  error = NULL;
  draw();
}
