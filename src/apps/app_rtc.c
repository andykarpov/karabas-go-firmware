#include "config.h"
#include "types.h"
#include "main.h"
#include "osd.h"
#include "input/usb_hid_keys.h"
#include "input/sega.h"
#include "masked_input.h"
#include "time_zone.h"
#include "apps/app_rtc.h"

// Setup > Setup RTC: the date and the time are typed into masked fields. Up / Down / Tab move
// between the fields and the buttons, Left / Right move the cursor (or pick a button). The BOOT
// core (the ESP is behind it) has the time zone (karabas.ini [setup] tz) and NTP sync, which sets
// the clock from NTP; Save and Sync write the time zone, Cancel drops it.

enum { F_DATE, F_TIME, F_TZ, F_SAVE, F_CANCEL, F_SYNC, F_COUNT };

#define ROW_DATE 8
#define ROW_TIME 10
#define ROW_TZ 12
#define ROW_BUTTONS 14
#define ROW_ERROR 16
#define ROW_HINT 18
#define FIELD_X 8

static kg_field_t date_field, time_field;
static uint8_t focus;
static const char *error;
static int16_t tz;

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
  osd_set_pos(1, y); osd_print(label);
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

// as a choice of Settings: '<' UTC+03:00 '>'
static void draw_tz(bool focused) {
  char v[10];
  kg_tz_format(tz, v, sizeof(v));
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_set_pos(1, ROW_TZ); osd_print("Zone");
  // the value under the digits of the fields, the arrows around it
  osd_fill(FIELD_X - 1, ROW_TZ, FIELD_X + 9, ROW_TZ, ' ');
  if (focused) {
    osd_set_pos(FIELD_X - 1, ROW_TZ); osd_print("<");
    osd_set_pos(FIELD_X + 9, ROW_TZ); osd_print(">");
    osd_set_color(OSD_COLOR_BLACK, OSD_COLOR_YELLOW_I);
  } else {
    osd_set_color(OSD_COLOR_CYAN_I, OSD_COLOR_BLACK);
  }
  osd_set_pos(FIELD_X, ROW_TZ); osd_print("UTC"); osd_print(v);
  osd_set_color(OSD_COLOR_GREY, OSD_COLOR_BLACK);
  osd_set_pos(FIELD_X + 11, ROW_TZ); osd_print("for NTP sync");
}

static void draw() {
  draw_field(ROW_DATE, "Date", "DD.MM.YYYY", &date_field, focus == F_DATE);
  draw_field(ROW_TIME, "Time", "HH:MM", &time_field, focus == F_TIME);
  if (has_ntp()) {
    draw_tz(focus == F_TZ);
    ui_button(1, ROW_BUTTONS, 9, "Save", focus == F_SAVE);
    ui_button(11, ROW_BUTTONS, 9, "Cancel", focus == F_CANCEL);
    ui_button(21, ROW_BUTTONS, 9, "NTP sync", focus == F_SYNC);
  } else {
    ui_button(1, ROW_BUTTONS, 10, "Save", focus == F_SAVE);
    ui_button(12, ROW_BUTTONS, 10, "Cancel", focus == F_CANCEL);
  }
  const char *status = error ? error : ntp_sync_message();
  uint8_t ntp = ntp_sync_state();
  osd_set_color(error || ntp == NTP_ERROR ? OSD_COLOR_RED_I : ntp == NTP_OK ? OSD_COLOR_GREEN_I : OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_fill(0, ROW_ERROR, 31, ROW_ERROR, ' ');
  if (status) { osd_set_pos(1, ROW_ERROR); osd_print(status); }
  osd_set_color(OSD_COLOR_GREY, OSD_COLOR_BLACK);
  osd_fill(0, ROW_HINT, 31, ROW_HINT, ' ');
  osd_set_pos(1, ROW_HINT);
  if (focus == F_TZ) osd_print("UTC offset, no summer time");
  else if (focus == F_SYNC) osd_print("The time from NTP over Wi-Fi");
  osd_update();
}

// the fields from the RTC
static void init_fields() {
  char buf[11];
  snprintf(buf, sizeof(buf), "%02u.%02u.%04u", rtc_get_day(), rtc_get_month(), 2000 + rtc_get_year());
  kg_field_init(&date_field, buf);
  snprintf(buf, sizeof(buf), "%02u:%02u", rtc_get_hour(), rtc_get_minute());
  kg_field_init(&time_field, buf);
}

void app_rtc_overlay() {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_clear();
  osd_header(core.build, core.id, HW_ID);
  print_time();
  osd_set_pos(0, 5);
  osd_print("Setup RTC");
  osd_line(6);

  init_fields();
  focus = F_DATE;
  error = NULL;
  tz = hw_setup.tz;
  ntp_sync_cancel();

  // footer
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_line(23);
  osd_set_pos(1, 24); osd_print("Digits: edit, Up/Down: next");
  osd_set_pos(1, 25); osd_print("Enter: OK, Esc: cancel");
  draw();
}

// the time zone into karabas.ini when it changed
static bool save_tz() {
  if (tz == hw_setup.tz) return true;
  setup_t n = hw_setup;
  n.tz = tz;
  if (save_setup(&n)) return true;
  error = "Can't save karabas.ini";
  return false;
}

static bool save() {
  int day, month, year, hour, minute;
  if (!kg_date_parse(date_field.text, &day, &month, &year)) {
    focus = F_DATE;
    error = "Invalid date";
    return false;
  }
  if (!kg_time_parse(time_field.text, &hour, &minute)) {
    focus = F_TIME;
    error = "Invalid time";
    return false;
  }
  if (!save_tz()) return false;
  rtc_set_day(day);
  rtc_set_month(month);
  rtc_set_year(year - 2000);
  rtc_set_hour(hour);
  rtc_set_minute(minute);
  rtc_set_second(0);
  rtc_save();
  return true;
}

// the digit of a key, 0 if it is not a digit key
static char key_digit(uint8_t key) {
  if (key >= KEY_1 && key <= KEY_9) return '1' + (key - KEY_1);
  if (key == KEY_0) return '0';
  if (key >= KEY_KP1 && key <= KEY_KP9) return '1' + (key - KEY_KP1);
  if (key == KEY_KP0) return '0';
  return 0;
}

static void leave() {
  ntp_sync_cancel();
  osd_state = state_setup;
}

void app_rtc_on_keyboard() {
  uint8_t key = usb_keyboard_report.keycode[0];
  bool up = key == KEY_UP || (joyL & SC_BTN_UP) || (joyR & SC_BTN_UP);
  bool down = key == KEY_DOWN || key == KEY_TAB || (joyL & SC_BTN_DOWN) || (joyR & SC_BTN_DOWN);
  bool left = key == KEY_LEFT || (joyL & SC_BTN_LEFT) || (joyR & SC_BTN_LEFT);
  bool right = key == KEY_RIGHT || (joyL & SC_BTN_RIGHT) || (joyR & SC_BTN_RIGHT);
  bool enter = key == KEY_ENTER || key == KEY_KPENTER || (joyL & SC_BTN_A) || (joyR & SC_BTN_A) || (joyL & SC_BTN_B) || (joyR & SC_BTN_B);
  bool esc = key == KEY_ESC || ((joyL | joyR) & UI_JOY_ESC);
  kg_field_t *field = focus == F_DATE ? &date_field : focus == F_TIME ? &time_field : NULL;
  bool button = focus >= F_SAVE;
  char digit = key_digit(key);

  if (esc) {
    leave();
    return;
  }
  if (field && !key && (up || down)) {
    // the joystick has no digits: Up / Down turn the one under the cursor
    error = NULL;
    kg_field_turn(field, up ? 1 : -1);
  } else if (up) {
    // the buttons are one row: from them to the last field, from the first field to the last button
    focus = focus_step(button ? F_SAVE : focus, -1);
  } else if (down) {
    focus = focus_step(focus, 1);
  } else if (field && digit) {
    error = NULL;
    if (kg_field_type(field, digit)) focus = focus_step(focus, 1);
  } else if (field && (left || key == KEY_BACKSPACE)) {
    kg_field_left(field);
  } else if (field && right) {
    kg_field_right(field);
  } else if (focus == F_TZ && (left || right)) {
    tz = kg_tz_step(tz, right ? 1 : -1);
  } else if (button && (left || right)) {
    do focus = F_SAVE + (focus - F_SAVE + (right ? 1 : 2)) % 3; while (!focusable(focus));
  } else if (enter && !button) {
    focus = focus_step(focus, 1);
  } else if (enter && focus == F_SAVE) {
    if (save()) { leave(); return; }
  } else if (enter && focus == F_CANCEL) {
    leave();
    return;
  } else if (enter && focus == F_SYNC) {
    error = NULL;
    if (save_tz()) ntp_sync_start(tz);
  } else {
    return;
  }
  draw();
}

void app_rtc_tick() {
  if (ntp_sync_state() != NTP_BUSY) return;
  uint8_t r = ntp_sync_poll();
  if (r == NTP_BUSY) return;
  if (r == NTP_OK) init_fields(); // the time typed before is gone
  draw();
}
