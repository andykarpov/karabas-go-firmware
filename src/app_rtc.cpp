#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "OSD.h"
#include "usb_hid_keys.h"
#include "SegaController.h"
#include "masked_input.h"
#include "app_rtc.h"

// Setup > Setup RTC: the date and the time are typed into masked fields. Up / Down / Tab move
// between the fields and the buttons, Left / Right move the cursor (or pick a button).

enum { F_DATE, F_TIME, F_SAVE, F_CANCEL, F_COUNT };

#define ROW_DATE 8
#define ROW_TIME 10
#define ROW_BUTTONS 13
#define ROW_ERROR 15
#define FIELD_X 8

static kg_field_t date_field, time_field;
static uint8_t focus;
static const char *error;

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

static void draw() {
  draw_field(ROW_DATE, "Date", "DD.MM.YYYY", &date_field, focus == F_DATE);
  draw_field(ROW_TIME, "Time", "HH:MM", &time_field, focus == F_TIME);
  ui_button(1, ROW_BUTTONS, 10, "Save", focus == F_SAVE);
  ui_button(12, ROW_BUTTONS, 10, "Cancel", focus == F_CANCEL);
  zxosd.setColor(OSD::COLOR_RED_I, OSD::COLOR_BLACK);
  zxosd.fill(0, ROW_ERROR, 31, ROW_ERROR, ' ');
  if (error) { zxosd.setPos(1, ROW_ERROR); zxosd.print(error); }
  zxosd.update();
}

void app_rtc_overlay() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();
  zxosd.header(core.build, core.id, HW_ID);
  print_time();
  zxosd.setPos(0, 5);
  zxosd.print("Setup RTC");
  zxosd.line(6);

  char buf[11];
  snprintf(buf, sizeof(buf), "%02u.%02u.%04u", zxrtc.getDay(), zxrtc.getMonth(), 2000 + zxrtc.getYear());
  kg_field_init(&date_field, buf);
  snprintf(buf, sizeof(buf), "%02u:%02u", zxrtc.getHour(), zxrtc.getMinute());
  kg_field_init(&time_field, buf);
  focus = F_DATE;
  error = nullptr;

  // footer
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.line(23);
  zxosd.setPos(1, 24); zxosd.print("Digits: edit, Up/Down: next");
  zxosd.setPos(1, 25); zxosd.print("Enter: OK, Esc: cancel");
  draw();
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

void app_rtc_on_keyboard() {
  uint8_t key = usb_keyboard_report.keycode[0];
  bool up = key == KEY_UP || (joyL & SC_BTN_UP) || (joyR & SC_BTN_UP);
  bool down = key == KEY_DOWN || key == KEY_TAB || (joyL & SC_BTN_DOWN) || (joyR & SC_BTN_DOWN);
  bool left = key == KEY_LEFT || (joyL & SC_BTN_LEFT) || (joyR & SC_BTN_LEFT);
  bool right = key == KEY_RIGHT || (joyL & SC_BTN_RIGHT) || (joyR & SC_BTN_RIGHT);
  bool enter = key == KEY_ENTER || key == KEY_KPENTER || (joyL & SC_BTN_A) || (joyR & SC_BTN_A) || (joyL & SC_BTN_B) || (joyR & SC_BTN_B);
  bool esc = key == KEY_ESC || (joyL & SC_BTN_C) || (joyR & SC_BTN_C);
  kg_field_t *field = focus == F_DATE ? &date_field : focus == F_TIME ? &time_field : nullptr;
  char digit = key_digit(key);

  if (esc) {
    osd_state = state_setup;
    return;
  }
  if (field && !key && (up || down)) {
    // the joystick has no digits: Up / Down turn the one under the cursor
    error = nullptr;
    kg_field_turn(field, up ? 1 : -1);
  } else if (up) {
    focus = focus == F_DATE ? F_CANCEL : focus == F_CANCEL ? F_TIME : focus - 1;
  } else if (down) {
    focus = (focus + 1) % F_COUNT;
  } else if (field && digit) {
    error = nullptr;
    if (kg_field_type(field, digit)) focus++;
  } else if (field && left) {
    kg_field_left(field);
  } else if (field && key == KEY_BACKSPACE) {
    kg_field_left(field);
  } else if (field && right) {
    kg_field_right(field);
  } else if (!field && (left || right)) {
    focus = focus == F_SAVE ? F_CANCEL : F_SAVE;
  } else if (enter && field) {
    focus++;
  } else if (enter && focus == F_SAVE) {
    if (save()) { osd_state = state_setup; return; }
  } else if (enter && focus == F_CANCEL) {
    osd_state = state_setup;
    return;
  } else {
    return;
  }
  draw();
}
