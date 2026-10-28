#include "config.h"
#include "types.h"
#include "main.h"
#include "osd.h"
#include "input/usb_hid_keys.h"
#include "input/sega.h"
#include "masked_input.h"
#include "time_zone.h"
#include "apps/app_rtc.h"
#include "ui/ui_draw.h"
#include "apps/clock_edit.h"

// Setup > Setup RTC: the date and the time are typed into masked fields. Up / Down / Tab move
// between the fields and the buttons, Left / Right move the cursor (or pick a button). The BOOT
// core (the ESP is behind it) has the time zone (karabas.ini [setup] tz) and NTP sync, which sets
// the clock from NTP; Save and Sync write the time zone, Cancel drops it. The editing itself is
// apps/clock_edit, shared with the Clock page of the graphic setup.

enum { F_DATE, F_TIME, F_TZ, F_SAVE, F_CANCEL, F_SYNC, F_COUNT };

#define ROW_DATE 8
#define ROW_TIME 10
#define ROW_TZ 12
#define ROW_BUTTONS 14
#define ROW_ERROR 16
#define ROW_HINT 18
#define FIELD_X 6
#define TZ_WIDTH 10 // the selector of the zone, as wide as the date field
#define BTN_WIDTH 10

static uint8_t focus;

// the time zone and Sync
static bool has_ntp() {
  return core.type == CORE_TYPE_BOOT;
}

static bool focusable(uint8_t f) {
  return has_ntp() || (f != F_TZ && f != F_SYNC);
}

// the next focusable item, round
static uint8_t focus_step(uint8_t f, int d) {
  do f = (f + F_COUNT + d) % F_COUNT; while (!focusable(f));
  return f;
}

static void draw_field(uint8_t y, const char *label, const char *hint, const kg_field_t *f, bool focused) {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_set_pos(0, y); osd_print(label);
  osd_set_pos(FIELD_X, y);
  for (uint8_t i = 0; i < f->len; i++) {
    if (focused && i == f->pos) {
      osd_set_color(OSD_COLOR_BLACK, OSD_COLOR_CYAN_I);
    } else if (focused) {
      osd_set_color(OSD_COLOR_BLACK, OSD_COLOR_WHITE);
    } else {
      osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
    }
    osd_putc(f->text[i]);
  }
  osd_set_color(OSD_COLOR_GREY, OSD_COLOR_BLACK);
  osd_set_pos(FIELD_X + f->len + 2, y); osd_print(hint);
}

// as a choice of Settings: UTC+03:00 under the digits of the fields, on yellow when focused
static void draw_tz(bool focused) {
  char v[TZ_WIDTH + 1], tz[10];
  kg_tz_format(ce_tz(), tz, sizeof(tz));
  snprintf(v, sizeof(v), "UTC%-*s", TZ_WIDTH - 3, tz);
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_set_pos(0, ROW_TZ); osd_print("Zone");
  if (focused) osd_set_color(OSD_COLOR_BLACK, OSD_COLOR_YELLOW_I);
  else osd_set_color(OSD_COLOR_CYAN_I, OSD_COLOR_BLACK);
  osd_set_pos(FIELD_X, ROW_TZ); osd_print(v);
  osd_set_color(OSD_COLOR_GREY, OSD_COLOR_BLACK);
  osd_set_pos(FIELD_X + TZ_WIDTH + 2, ROW_TZ); osd_print("for NTP sync");
}

static void draw() {
  draw_field(ROW_DATE, "Date", "DD.MM.YYYY", ce_field(CE_DATE), focus == F_DATE);
  draw_field(ROW_TIME, "Time", "HH:MM", ce_field(CE_TIME), focus == F_TIME);
  if (has_ntp()) draw_tz(focus == F_TZ);
  // the labels have even lengths, so they center exactly in the even width
  ui_button(0, ROW_BUTTONS, BTN_WIDTH, "Save", focus == F_SAVE);
  ui_button(BTN_WIDTH + 1, ROW_BUTTONS, BTN_WIDTH, "Cancel", focus == F_CANCEL);
  if (has_ntp()) ui_button(2 * (BTN_WIDTH + 1), ROW_BUTTONS, BTN_WIDTH, "NTP sync", focus == F_SYNC);
  uint8_t kind;
  const char *status = ce_message(&kind);
  osd_set_color(kind == CE_MSG_ERROR ? OSD_COLOR_RED_I : kind == CE_MSG_OK ? OSD_COLOR_GREEN_I : OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_fill(0, ROW_ERROR, UI_COL_LAST, ROW_ERROR, ' ');
  if (status) { osd_set_pos(0, ROW_ERROR); osd_print(status); }
  osd_set_color(OSD_COLOR_GREY, OSD_COLOR_BLACK);
  osd_fill(0, ROW_HINT, UI_COL_LAST, ROW_HINT, ' ');
  osd_set_pos(0, ROW_HINT);
  if (focus == F_TZ) osd_print("UTC offset, no summer time");
  else if (focus == F_SYNC) osd_print("The time from NTP over Wi-Fi");
  osd_update();
}

void app_rtc_overlay() {
  ui_screen("Setup RTC");

  ce_open();
  focus = F_DATE;

  ui_hint("Digits: edit, Up/Down: next", "Enter: OK, Esc: cancel");
  draw();
}

static void leave() {
  ce_close();
  osd_state = state_setup;
}

void app_rtc_on_keyboard() {
  uint8_t typed = usb_keyboard_report.keycode[0]; // 0: the key comes from the joystick
  uint8_t key = ui_nav_key();
  bool up = key == KEY_UP;
  bool down = key == KEY_DOWN || key == KEY_TAB;
  bool left = key == KEY_LEFT;
  bool right = key == KEY_RIGHT;
  bool enter = key == KEY_ENTER;
  bool esc = key == KEY_ESC;
  bool field = focus == F_DATE || focus == F_TIME;
  uint8_t f = focus == F_TIME ? CE_TIME : CE_DATE;
  bool button = focus >= F_SAVE;
  char digit = ce_key_digit(typed);

  if (esc) {
    leave();
    return;
  }
  if (field && !typed && (up || down)) {
    // the joystick has no digits: Up / Down turn the one under the cursor
    ce_turn(f, up ? 1 : -1);
  } else if (up) {
    // the buttons are one row: from them to the last field, from the first field to the last button
    focus = focus_step(button ? F_SAVE : focus, -1);
  } else if (down) {
    focus = focus_step(focus, 1);
  } else if (field && digit) {
    if (ce_type(f, digit)) focus = focus_step(focus, 1);
  } else if (field && (left || key == KEY_BACKSPACE)) {
    kg_field_left(ce_field(f));
  } else if (field && right) {
    kg_field_right(ce_field(f));
  } else if (focus == F_TZ && (left || right)) {
    ce_tz_step(right ? 1 : -1);
  } else if (button && (left || right)) {
    do focus = F_SAVE + (focus - F_SAVE + (right ? 1 : 2)) % 3; while (!focusable(focus));
  } else if (enter && !button) {
    focus = focus_step(focus, 1);
  } else if (enter && focus == F_SAVE) {
    uint8_t bad;
    if (ce_set_clock(&bad)) { leave(); return; }
    if (bad != CE_NO_FIELD) focus = bad == CE_TIME ? F_TIME : F_DATE;
  } else if (enter && focus == F_CANCEL) {
    leave();
    return;
  } else if (enter && focus == F_SYNC) {
    ce_sync();
  } else {
    return;
  }
  draw();
}

void app_rtc_tick() {
  if (ce_tick()) draw();
}
