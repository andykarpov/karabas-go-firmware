#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "FT812.h"
#include "usb_hid_keys.h"
#include "masked_input.h"
#include "time_zone.h"
#include "ui/ft_mouse.h"
#include "ui/ft_page.h"
#include "app_ft_pages.h"
#include "app_ft_setup.h"

// The Clock page of the graphic setup: the date and the time typed into masked fields (as in
// Setup RTC of the text menu), Set writes them to the RTC, Sync sets it from NTP (ntp_sync) with
// the time zone of the page (karabas.ini [setup] tz, saved when the page is left, by Set and by
// NTP sync). A click on a digit puts the cursor there, the wheel turns the digit under the pointer
// (or the cursor).

enum { F_DATE, F_TIME, F_TZ, F_SET, F_SYNC, F_COUNT };

// the clickable areas
enum { HIT_FIELD = 0, HIT_TZ = 2, HIT_TZ_DOWN, HIT_TZ_UP, HIT_SET, HIT_SYNC, HIT_CHAR = 10 }; // HIT_CHAR + 20 * field + char

#define TOP_GAP 10 // under the title line
#define ROW_H FT_PAGE_FIELD_H
#define ROW_STEP 40
#define FIELD_X 120
#define CHAR_W 14
#define TZ_W 160 // '<', the offset, '>'
#define MESSAGE_MS 5000
#define CURSOR_COLOR 0x00d8d8

static kg_field_t fields[2];
static bool edited[2]; // a field not edited follows the RTC
static uint8_t focus;
static const char *message;
static bool message_ok;
static bool message_busy; // the NTP sync runs
static uint32_t message_timer = 0;
static int16_t tz; // the time zone of the page, minutes

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
  tz = hw_setup.tz;
  ntp_sync_cancel();
}

// the time zone into karabas.ini when it changed; the graphic setup keeps it in its copy too
static bool save_tz() {
  if (tz == hw_setup.tz) return true;
  setup_t n = hw_setup;
  n.tz = tz;
  if (!save_setup(n)) {
    message = "Can't save karabas.ini";
    message_ok = message_busy = false;
    return false;
  }
  app_ft_setup_tz_saved(tz);
  return true;
}

static void close() {
  ntp_sync_cancel();
  save_tz();
}

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

// the time zone: '<' UTC+03:00 '>'
static void draw_tz(const ft_page_ctx_t &c, int16_t y) {
  bool focused = c.focused && focus == F_TZ;
  int16_t vx = c.x + FIELD_X + 40;
  // the arrows before the row: the first area under the pointer gets the click
  bool hovered = false;
  if (c.focused) {
    hovered |= ft_page_hit(vx, y, 32, ROW_H, HIT_TZ_DOWN);
    hovered |= ft_page_hit(vx + TZ_W - 32, y, 32, ROW_H, HIT_TZ_UP);
    hovered |= ft_page_hit(c.x, y, c.w, ROW_H, HIT_TZ);
  }
  ft_page_row(c, c.x, y, c.w, ROW_H, focused, hovered);
  ft.drawText(c.x + 16, y + ROW_H / 2, FT_PAGE_FONT, c.s->color_text, FT81x_OPT_CENTERY, "Time zone");
  char v[12] = "UTC";
  kg_tz_format(tz, v + 3, sizeof(v) - 3);
  ft.drawText(vx + TZ_W / 2, y + ROW_H / 2, FT_PAGE_FONT, c.s->color_text, FT81x_OPT_CENTER, v);
  if (focused || hovered) {
    ft.drawText(vx + 12, y + ROW_H / 2, FT_PAGE_FONT, c.s->color_text, FT81x_OPT_CENTER, "<");
    ft.drawText(vx + TZ_W - 12, y + ROW_H / 2, FT_PAGE_FONT, c.s->color_text, FT81x_OPT_CENTER, ">");
  }
}

static void draw(const ft_page_ctx_t &c) {
  if (!c.focused) {
    edited[F_DATE] = edited[F_TIME] = false;
    tz = c.s->tz;
  }
  init_fields(); // the RTC as it runs
  draw_field(c, F_DATE, c.y + TOP_GAP, "Date", "DD.MM.YYYY");
  draw_field(c, F_TIME, c.y + TOP_GAP + ROW_STEP, "Time", "HH:MM");
  draw_tz(c, c.y + TOP_GAP + 2 * ROW_STEP);
  int16_t y = c.y + TOP_GAP + 3 * ROW_STEP + 8;
  ft_page_button(c, c.x + 16, y, 140, "Set", focus == F_SET, HIT_SET);
  ft_page_button(c, c.x + 172, y, 140, "NTP sync", focus == F_SYNC, HIT_SYNC);
  if (message && c.focused) {
    // under the buttons: the panel is not wide enough beside them
    uint32_t color = message_busy ? c.s->color_text : message_ok ? FT_PAGE_OK : FT_PAGE_ERROR;
    ft.drawText(c.x + 16, y + FT_PAGE_BTN_H + 24, FT_PAGE_FONT, color, FT81x_OPT_CENTERY, message);
  }
}

static void set_clock() {
  int day, month, year, hour, minute;
  ntp_sync_cancel();
  message_ok = message_busy = false;
  if (!save_tz()) return;
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
  message_timer = ms_now();
}

static void sync_clock() {
  if (!save_tz()) return;
  ntp_sync_start(tz);
  message = ntp_sync_message();
  message_ok = false;
  message_busy = true;
}

// the end of the NTP sync; the message goes after a while
static bool tick() {
  if (ntp_sync_state() == NTP_BUSY) {
    uint8_t r = ntp_sync_poll();
    if (r == NTP_BUSY) return false;
    if (r == NTP_OK) edited[F_DATE] = edited[F_TIME] = false; // the RTC as it is set
    message = ntp_sync_message();
    message_ok = r == NTP_OK;
    message_busy = false;
    message_timer = ms_now();
    return true;
  }
  if (!message || !message_ok || ms_since(message_timer) < MESSAGE_MS) return false;
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
  kg_field_t *field = focus < F_TZ ? &fields[focus] : nullptr;
  bool enter = key == KEY_ENTER || key == KEY_KPENTER;
  char digit = key_digit(key);
  if (key == KEY_ESC) return false;
  if ((digit || enter) && !message_busy) message = nullptr;

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
  else if (focus == F_TZ && (key == KEY_LEFT || key == KEY_RIGHT)) tz = kg_tz_step(tz, key == KEY_RIGHT ? 1 : -1);
  else if (focus == F_TZ && enter) focus++;
  else if (key == KEY_LEFT || key == KEY_RIGHT) focus = focus == F_SET ? F_SYNC : F_SET;
  else if (enter && focus == F_SYNC) sync_clock();
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
  } else if (id == HIT_TZ) {
    focus = F_TZ;
  } else if (id == HIT_TZ_DOWN || id == HIT_TZ_UP) {
    focus = F_TZ;
    tz = kg_tz_step(tz, id == HIT_TZ_UP ? 1 : -1);
  } else if (id == HIT_SET) {
    focus = F_SET;
    set_clock();
  } else if (id == HIT_SYNC) {
    focus = F_SYNC;
    sync_clock();
  }
  return true;
}

static void wheel(int16_t id, int d) {
  if (id >= HIT_CHAR && id < HIT_CHAR + 40) {
    focus = (id - HIT_CHAR) / 20;
    put_cursor(&fields[focus], (id - HIT_CHAR) % 20);
  } else if (id == HIT_FIELD + F_DATE || id == HIT_FIELD + F_TIME) {
    focus = id - HIT_FIELD;
  } else if (id >= HIT_TZ && id <= HIT_TZ_UP) {
    focus = F_TZ;
    tz = kg_tz_step(tz, d > 0 ? 1 : -1);
    return;
  } else {
    return;
  }
  message = nullptr;
  edited[focus] = true;
  kg_field_turn(&fields[focus], d);
}

static const char *hint() {
  if (focus == F_TZ) return "Left/Right: the UTC offset of NTP sync, no summer time";
  if (focus == F_SYNC) return "Set the clock from NTP over Wi-Fi";
  return focus == F_SET ? "Write the date and the time to the RTC" : "Digits (pad: Up/Down): type   Left/Right: cursor";
}

static const char *keys() { return "Up/Down: select   Enter: next / set   Esc: back"; }

const ft_page_t ft_page_clock = { open, close, nullptr, nullptr, draw, key, tick, click, wheel, hint, keys };
