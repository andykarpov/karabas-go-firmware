#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "FT812.h"
#include "usb_hid_keys.h"
#include "masked_input.h"
#include "ui/ft_mouse.h"
#include "ui/ft_page.h"
#include "app_ft_pages.h"

// The Clock page of the graphic setup: the date and the time typed into masked fields (as in
// Setup RTC of the text menu), Set writes them to the RTC. A click on a digit puts the cursor
// there, the wheel turns the digit under the pointer (or the cursor).

enum { F_DATE, F_TIME, F_SET, F_COUNT };

// the clickable areas
enum { HIT_FIELD = 0, HIT_SET = 2, HIT_CHAR = 10 }; // HIT_CHAR + 20 * field + char

#define TOP_GAP 10 // under the title line
#define ROW_H FT_PAGE_FIELD_H
#define ROW_STEP 40
#define FIELD_X 120
#define CHAR_W 14
#define MESSAGE_MS 5000
#define CURSOR_COLOR 0x00d8d8

static kg_field_t fields[2];
static bool edited[2]; // a field not edited follows the RTC
static uint8_t focus;
static const char *message;
static bool message_ok;
static ElapsedTimer message_timer;

// the fields not edited from the RTC, the cursor stays
static void init_fields() {
  char buf[11];
  uint8_t pos;
  if (!edited[F_DATE]) {
    snprintf(buf, sizeof(buf), "%02u.%02u.%04u", zxrtc.getDay(), zxrtc.getMonth(), 2000 + zxrtc.getYear());
    pos = fields[F_DATE].pos;
    kg_field_init(&fields[F_DATE], buf);
    fields[F_DATE].pos = pos < fields[F_DATE].len ? pos : 0;
  }
  if (!edited[F_TIME]) {
    snprintf(buf, sizeof(buf), "%02u:%02u", zxrtc.getHour(), zxrtc.getMinute());
    pos = fields[F_TIME].pos;
    kg_field_init(&fields[F_TIME], buf);
    fields[F_TIME].pos = pos < fields[F_TIME].len ? pos : 0;
  }
}

static void open() {
  edited[F_DATE] = edited[F_TIME] = false;
  fields[F_DATE].pos = fields[F_TIME].pos = 0;
  init_fields();
  focus = F_DATE;
  message = nullptr;
}

static void close() {}

static void draw_field(const ft_page_ctx_t &c, uint8_t i, int16_t y, const char *label, const char *hint) {
  const kg_field_t *f = &fields[i];
  bool focused = c.focused && focus == i;
  int16_t fx = c.x + FIELD_X, fw = f->len * CHAR_W + 16;
  for (uint8_t j = 0; j < f->len && c.focused; j++) ft_page_hit(fx + 8 + j * CHAR_W, y, CHAR_W, ROW_H, HIT_CHAR + 20 * i + j);
  bool hovered = c.focused && ft_page_hit(c.x, y, c.w, ROW_H, HIT_FIELD + i);
  ft_page_row(c, c.x, y, c.w, ROW_H, focused, hovered);
  ft.drawText(c.x + 16, y + ROW_H / 2, FT_PAGE_FONT, c.s->color_text, FT81x_OPT_CENTERY, label);
  if (focused) ft_page_rect(fx - 2, y - 2, fw + 4, ROW_H + 4, 4, c.s->color_active);
  ft_page_rect(fx, y, fw, ROW_H, 4, c.s->color_bg);
  for (uint8_t j = 0; j < f->len; j++) {
    int16_t cx = fx + 8 + j * CHAR_W;
    uint32_t color = c.s->color_text;
    if (focused && j == f->pos) {
      ft.drawRect(cx, y + 4, CHAR_W, ROW_H - 8, 1, CURSOR_COLOR);
      color = 0x000000;
    }
    char ch[2] = { f->text[j], '\0' };
    ft.drawText(cx + CHAR_W / 2, y + ROW_H / 2, FT_PAGE_FONT_SMALL, color, FT81x_OPT_CENTER, ch);
  }
  ft.drawText(fx + fw + 16, y + ROW_H / 2, FT_PAGE_FONT_SMALL, c.s->color_copyright, FT81x_OPT_CENTERY, hint);
}

static void draw(const ft_page_ctx_t &c) {
  if (!c.focused) edited[F_DATE] = edited[F_TIME] = false;
  init_fields(); // the RTC as it runs
  draw_field(c, F_DATE, c.y + TOP_GAP, "Date", "DD.MM.YYYY");
  draw_field(c, F_TIME, c.y + TOP_GAP + ROW_STEP, "Time", "HH:MM");
  int16_t y = c.y + TOP_GAP + 2 * ROW_STEP + 8;
  ft_page_button(c, c.x + 16, y, 140, "Set", focus == F_SET, HIT_SET);
  if (message && c.focused) {
    ft.drawText(c.x + 176, y + FT_PAGE_BTN_H / 2, FT_PAGE_FONT, message_ok ? FT_PAGE_OK : FT_PAGE_ERROR, FT81x_OPT_CENTERY, message);
  }
}

static void set_clock() {
  int day, month, year, hour, minute;
  message_ok = false;
  if (!kg_date_parse(fields[F_DATE].text, &day, &month, &year)) {
    focus = F_DATE;
    message = "Invalid date";
    return;
  }
  if (!kg_time_parse(fields[F_TIME].text, &hour, &minute)) {
    focus = F_TIME;
    message = "Invalid time";
    return;
  }
  zxrtc.setDay(day);
  zxrtc.setMonth(month);
  zxrtc.setYear(year - 2000);
  zxrtc.setHour(hour);
  zxrtc.setMinute(minute);
  zxrtc.setSecond(0);
  zxrtc.save();
  edited[F_DATE] = edited[F_TIME] = false;
  message = "The clock is set";
  message_ok = true;
  message_timer.reset();
}

// the message goes after a while
static bool tick() {
  if (!message || !message_ok || message_timer.elapsed() < MESSAGE_MS) return false;
  message = nullptr;
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

static bool key(uint8_t key, bool shift, bool joy) {
  kg_field_t *field = focus < F_SET ? &fields[focus] : nullptr;
  bool enter = key == KEY_ENTER || key == KEY_KPENTER;
  char digit = key_digit(key);
  if (key == KEY_ESC) return false;
  if (digit || enter) message = nullptr;

  if (joy && field && (key == KEY_UP || key == KEY_DOWN)) {
    // the joystick has no digits: Up / Down turn the one under the cursor
    message = nullptr;
    edited[focus] = true;
    kg_field_turn(field, key == KEY_UP ? 1 : -1);
  } else if (key == KEY_UP) focus = (focus + F_COUNT - 1) % F_COUNT;
  else if (key == KEY_DOWN || key == KEY_TAB) focus = (focus + 1) % F_COUNT;
  else if (field && digit) { edited[focus] = true; if (kg_field_type(field, digit)) focus++; }
  else if (field && (key == KEY_LEFT || key == KEY_BACKSPACE)) kg_field_left(field);
  else if (field && key == KEY_RIGHT) kg_field_right(field);
  else if (field && enter) focus++;
  else if (enter) set_clock();
  return true;
}

// the cursor onto char j of a field (or the next digit when j is a separator)
static void put_cursor(kg_field_t *f, uint8_t j) {
  for (uint8_t i = 0; i < f->len && f->pos > j; i++) kg_field_left(f);
  for (uint8_t i = 0; i < f->len && f->pos < j; i++) {
    uint8_t was = f->pos;
    kg_field_right(f);
    if (f->pos == was) break;
  }
}

static bool click(int16_t id) {
  if (id >= HIT_CHAR && id < HIT_CHAR + 40) {
    focus = (id - HIT_CHAR) / 20;
    put_cursor(&fields[focus], (id - HIT_CHAR) % 20);
  } else if (id == HIT_FIELD + F_DATE || id == HIT_FIELD + F_TIME) {
    focus = id - HIT_FIELD;
  } else if (id == HIT_SET) {
    focus = F_SET;
    set_clock();
  }
  return true;
}

static void wheel(int16_t id, int d) {
  if (id >= HIT_CHAR && id < HIT_CHAR + 40) {
    focus = (id - HIT_CHAR) / 20;
    put_cursor(&fields[focus], (id - HIT_CHAR) % 20);
  } else if (id == HIT_FIELD + F_DATE || id == HIT_FIELD + F_TIME) {
    focus = id - HIT_FIELD;
  } else {
    return;
  }
  message = nullptr;
  edited[focus] = true;
  kg_field_turn(&fields[focus], d);
}

static const char *hint() {
  return focus == F_SET ? "Write the date and the time to the RTC" : "Digits (pad: Up/Down): type   Left/Right: cursor";
}

static const char *keys() { return "Up/Down: select   Enter: next / set   Esc: back"; }

const ft_page_t ft_page_clock = { open, close, nullptr, nullptr, draw, key, tick, click, wheel, hint, keys };
