#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "OSD.h"
#include "usb_hid_keys.h"
#include "input/sega.h"
#include "masked_input.h"
#include "time_zone.h"
#include "app_rtc.h"

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
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.setPos(1, y); zxosd.print(label);
  zxosd.setPos(FIELD_X, y);
  for (uint8_t i = 0; i < f->len; i++) {
    if (focused && i == f->pos) {
      zxosd.setColor(OSD::COLOR_BLACK, OSD::COLOR_CYAN_I);
    } else if (focused) {
      zxosd.setColor(OSD::COLOR_BLACK, OSD::COLOR_WHITE);
    } else {
      zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
    }
    zxosd.print(f->text[i]);
  }
  zxosd.setColor(OSD::COLOR_GREY, OSD::COLOR_BLACK);
  zxosd.setPos(FIELD_X + f->len + 2, y); zxosd.print(hint);
}

// as a choice of Settings: '<' UTC+03:00 '>'
static void draw_tz(bool focused) {
  char v[10];
  kg_tz_format(tz, v, sizeof(v));
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.setPos(1, ROW_TZ); zxosd.print("Zone");
  // the value under the digits of the fields, the arrows around it
  zxosd.fill(FIELD_X - 1, ROW_TZ, FIELD_X + 9, ROW_TZ, ' ');
  if (focused) {
    zxosd.setPos(FIELD_X - 1, ROW_TZ); zxosd.print("<");
    zxosd.setPos(FIELD_X + 9, ROW_TZ); zxosd.print(">");
    zxosd.setColor(OSD::COLOR_BLACK, OSD::COLOR_YELLOW_I);
  } else {
    zxosd.setColor(OSD::COLOR_CYAN_I, OSD::COLOR_BLACK);
  }
  zxosd.setPos(FIELD_X, ROW_TZ); zxosd.print("UTC"); zxosd.print(v);
  zxosd.setColor(OSD::COLOR_GREY, OSD::COLOR_BLACK);
  zxosd.setPos(FIELD_X + 11, ROW_TZ); zxosd.print("for NTP sync");
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
  zxosd.setColor(error || ntp == NTP_ERROR ? OSD::COLOR_RED_I : ntp == NTP_OK ? OSD::COLOR_GREEN_I : OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.fill(0, ROW_ERROR, 31, ROW_ERROR, ' ');
  if (status) { zxosd.setPos(1, ROW_ERROR); zxosd.print(status); }
  zxosd.setColor(OSD::COLOR_GREY, OSD::COLOR_BLACK);
  zxosd.fill(0, ROW_HINT, 31, ROW_HINT, ' ');
  zxosd.setPos(1, ROW_HINT);
  if (focus == F_TZ) zxosd.print("UTC offset, no summer time");
  else if (focus == F_SYNC) zxosd.print("The time from NTP over Wi-Fi");
  zxosd.update();
}

// the fields from the RTC
static void init_fields() {
  char buf[11];
  snprintf(buf, sizeof(buf), "%02u.%02u.%04u", zxrtc.getDay(), zxrtc.getMonth(), 2000 + zxrtc.getYear());
  kg_field_init(&date_field, buf);
  snprintf(buf, sizeof(buf), "%02u:%02u", zxrtc.getHour(), zxrtc.getMinute());
  kg_field_init(&time_field, buf);
}

void app_rtc_overlay() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();
  zxosd.header(core.build, core.id, HW_ID);
  print_time();
  zxosd.setPos(0, 5);
  zxosd.print("Setup RTC");
  zxosd.line(6);

  init_fields();
  focus = F_DATE;
  error = nullptr;
  tz = hw_setup.tz;
  ntp_sync_cancel();

  // footer
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.line(23);
  zxosd.setPos(1, 24); zxosd.print("Digits: edit, Up/Down: next");
  zxosd.setPos(1, 25); zxosd.print("Enter: OK, Esc: cancel");
  draw();
}

// the time zone into karabas.ini when it changed
static bool save_tz() {
  if (tz == hw_setup.tz) return true;
  setup_t n = hw_setup;
  n.tz = tz;
  if (save_setup(n)) return true;
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
  zxrtc.setDay(day);
  zxrtc.setMonth(month);
  zxrtc.setYear(year - 2000);
  zxrtc.setHour(hour);
  zxrtc.setMinute(minute);
  zxrtc.setSecond(0);
  zxrtc.save();
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
  kg_field_t *field = focus == F_DATE ? &date_field : focus == F_TIME ? &time_field : nullptr;
  bool button = focus >= F_SAVE;
  char digit = key_digit(key);

  if (esc) {
    leave();
    return;
  }
  if (field && !key && (up || down)) {
    // the joystick has no digits: Up / Down turn the one under the cursor
    error = nullptr;
    kg_field_turn(field, up ? 1 : -1);
  } else if (up) {
    // the buttons are one row: from them to the last field, from the first field to the last button
    focus = focus_step(button ? F_SAVE : focus, -1);
  } else if (down) {
    focus = focus_step(focus, 1);
  } else if (field && digit) {
    error = nullptr;
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
    error = nullptr;
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
