#include "config.h"
#include "types.h"
#include "main.h"
#include "ft812.h"
#include "input/usb_hid_keys.h"
#include "masked_input.h"
#include "time_zone.h"
#include "ui/ft_mouse.h"
#include "ui/ft_page.h"
#include "apps/app_ft_pages.h"
#include "apps/app_ft_setup.h"
#include "apps/clock_edit.h"

// The Clock page of the graphic setup: the date and the time typed into masked fields (as in
// Setup RTC of the text menu), Set writes them to the RTC, Sync sets it from NTP (ntp_sync) with
// the time zone of the page (karabas.ini [setup] tz, saved when the page is left, by Set and by
// NTP sync). A click on a digit puts the cursor there, the wheel turns the digit under the pointer
// (or the cursor). The editing itself is apps/clock_edit, shared with Setup RTC of the text menu.

enum { F_DATE = CE_DATE, F_TIME = CE_TIME, F_TZ, F_SET, F_SYNC, F_COUNT }; // the fields are those of clock_edit

// the clickable areas
enum { HIT_FIELD = 0, HIT_TZ = 2, HIT_TZ_DOWN, HIT_TZ_UP, HIT_SET, HIT_SYNC, HIT_CHAR = 10 }; // HIT_CHAR + 20 * field + char

#define TOP_GAP 10 // under the title line
#define ROW_H FT_PAGE_FIELD_H
#define ROW_STEP 40
#define FIELD_X 120
#define CHAR_W 14
#define TZ_W 160 // '<', the offset, '>'
#define CURSOR_COLOR 0x00d8d8

static uint8_t focus;

static void open() {
  ce_open();
  focus = F_DATE;
}

static void close() {
  ce_close();
  ce_save_tz(); // the time zone of the page is kept when it is left
}

static void draw_field(const ft_page_ctx_t *c, uint8_t i, int16_t y, const char *label, const char *hint) {
  const kg_field_t *f = ce_field(i);
  bool focused = c->focused && focus == i;
  int16_t fx = c->x + FIELD_X, fw = f->len * CHAR_W + 16;
  for (uint8_t j = 0; j < f->len && c->focused; j++) ft_page_hit(fx + 8 + j * CHAR_W, y, CHAR_W, ROW_H, HIT_CHAR + 20 * i + j);
  bool hovered = c->focused && ft_page_hit(c->x, y, c->w, ROW_H, HIT_FIELD + i);
  ft_page_row(c, c->x, y, c->w, ROW_H, focused, hovered);
  ft_draw_text(c->x + 16, y + ROW_H / 2, FT_PAGE_FONT, c->s->color_text, FT81x_OPT_CENTERY, label);
  if (focused) ft_page_rect(fx - 2, y - 2, fw + 4, ROW_H + 4, 4, c->s->color_active);
  ft_page_rect(fx, y, fw, ROW_H, 4, c->s->color_bg);
  for (uint8_t j = 0; j < f->len; j++) {
    int16_t cx = fx + 8 + j * CHAR_W;
    uint32_t color = c->s->color_text;
    if (focused && j == f->pos) {
      ft_draw_rect(cx, y + 4, CHAR_W, ROW_H - 8, 1, CURSOR_COLOR);
      color = 0x000000;
    }
    char ch[2] = { f->text[j], '\0' };
    ft_draw_text(cx + CHAR_W / 2, y + ROW_H / 2, FT_PAGE_FONT_SMALL, color, FT81x_OPT_CENTER, ch);
  }
  ft_draw_text(fx + fw + 16, y + ROW_H / 2, FT_PAGE_FONT_SMALL, c->s->color_copyright, FT81x_OPT_CENTERY, hint);
}

// the time zone: '<' UTC+03:00 '>'
static void draw_tz(const ft_page_ctx_t *c, int16_t y) {
  bool focused = c->focused && focus == F_TZ;
  int16_t vx = c->x + FIELD_X + 40;
  // the arrows before the row: the first area under the pointer gets the click
  bool hovered = false;
  if (c->focused) {
    hovered |= ft_page_hit(vx, y, 32, ROW_H, HIT_TZ_DOWN);
    hovered |= ft_page_hit(vx + TZ_W - 32, y, 32, ROW_H, HIT_TZ_UP);
    hovered |= ft_page_hit(c->x, y, c->w, ROW_H, HIT_TZ);
  }
  ft_page_row(c, c->x, y, c->w, ROW_H, focused, hovered);
  ft_draw_text(c->x + 16, y + ROW_H / 2, FT_PAGE_FONT, c->s->color_text, FT81x_OPT_CENTERY, "Time zone");
  char v[12] = "UTC";
  kg_tz_format(ce_tz(), v + 3, sizeof(v) - 3);
  ft_draw_text(vx + TZ_W / 2, y + ROW_H / 2, FT_PAGE_FONT, c->s->color_text, FT81x_OPT_CENTER, v);
  if (focused || hovered) {
    ft_draw_text(vx + 12, y + ROW_H / 2, FT_PAGE_FONT, c->s->color_text, FT81x_OPT_CENTER, "<");
    ft_draw_text(vx + TZ_W - 12, y + ROW_H / 2, FT_PAGE_FONT, c->s->color_text, FT81x_OPT_CENTER, ">");
  }
}

static void draw(const ft_page_ctx_t *c) {
  if (!c->focused) {
    ce_forget_edits();
    ce_set_tz(c->s->tz);
  }
  ce_follow_rtc(); // the RTC as it runs
  draw_field(c, F_DATE, c->y + TOP_GAP, "Date", "DD.MM.YYYY");
  draw_field(c, F_TIME, c->y + TOP_GAP + ROW_STEP, "Time", "HH:MM");
  draw_tz(c, c->y + TOP_GAP + 2 * ROW_STEP);
  int16_t y = c->y + TOP_GAP + 3 * ROW_STEP + 8;
  ft_page_button(c, c->x + 16, y, 140, "Set", focus == F_SET, HIT_SET);
  ft_page_button(c, c->x + 172, y, 140, "NTP sync", focus == F_SYNC, HIT_SYNC);
  uint8_t kind;
  const char *message = ce_message(&kind);
  if (message && c->focused) {
    // under the buttons: the panel is not wide enough beside them
    uint32_t color = kind == CE_MSG_BUSY ? c->s->color_text : kind == CE_MSG_OK ? FT_PAGE_OK : FT_PAGE_ERROR;
    ft_draw_text(c->x + 16, y + FT_PAGE_BTN_H + 24, FT_PAGE_FONT, color, FT81x_OPT_CENTERY, message);
  }
}

static void set_clock() {
  uint8_t bad;
  if (!ce_set_clock(&bad) && bad != CE_NO_FIELD) focus = bad;
}

// the end of the NTP sync; the message goes after a while
static bool tick() {
  return ce_tick();
}

static bool key(uint8_t key, bool shift, bool joy) {
  kg_field_t *field = focus < F_TZ ? ce_field(focus) : NULL;
  bool enter = key == KEY_ENTER || key == KEY_KPENTER;
  char digit = ce_key_digit(key);
  if (key == KEY_ESC) return false;
  if (digit || enter) ce_clear_message();

  if (joy && field && (key == KEY_UP || key == KEY_DOWN)) {
    // the joystick has no digits: Up / Down turn the one under the cursor
    ce_turn(focus, key == KEY_UP ? 1 : -1);
  } else if (key == KEY_UP) focus = (focus + F_COUNT - 1) % F_COUNT;
  else if (key == KEY_DOWN || key == KEY_TAB) focus = (focus + 1) % F_COUNT;
  else if (field && digit) { if (ce_type(focus, digit)) focus++; }
  else if (field && (key == KEY_LEFT || key == KEY_BACKSPACE)) kg_field_left(field);
  else if (field && key == KEY_RIGHT) kg_field_right(field);
  else if (field && enter) focus++;
  else if (focus == F_TZ && (key == KEY_LEFT || key == KEY_RIGHT)) ce_tz_step(key == KEY_RIGHT ? 1 : -1);
  else if (focus == F_TZ && enter) focus++;
  else if (key == KEY_LEFT || key == KEY_RIGHT) focus = focus == F_SET ? F_SYNC : F_SET;
  else if (enter && focus == F_SYNC) ce_sync();
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
    put_cursor(ce_field(focus), (id - HIT_CHAR) % 20);
  } else if (id == HIT_FIELD + F_DATE || id == HIT_FIELD + F_TIME) {
    focus = id - HIT_FIELD;
  } else if (id == HIT_TZ) {
    focus = F_TZ;
  } else if (id == HIT_TZ_DOWN || id == HIT_TZ_UP) {
    focus = F_TZ;
    ce_tz_step(id == HIT_TZ_UP ? 1 : -1);
  } else if (id == HIT_SET) {
    focus = F_SET;
    set_clock();
  } else if (id == HIT_SYNC) {
    focus = F_SYNC;
    ce_sync();
  }
  return true;
}

static void wheel(int16_t id, int d) {
  if (id >= HIT_CHAR && id < HIT_CHAR + 40) {
    focus = (id - HIT_CHAR) / 20;
    put_cursor(ce_field(focus), (id - HIT_CHAR) % 20);
  } else if (id == HIT_FIELD + F_DATE || id == HIT_FIELD + F_TIME) {
    focus = id - HIT_FIELD;
  } else if (id >= HIT_TZ && id <= HIT_TZ_UP) {
    focus = F_TZ;
    ce_tz_step(d > 0 ? 1 : -1);
    return;
  } else {
    return;
  }
  ce_turn(focus, d);
}

static const char *hint() {
  if (focus == F_TZ) return "Left/Right: the UTC offset of NTP sync, no summer time";
  if (focus == F_SYNC) return "Set the clock from NTP over Wi-Fi";
  return focus == F_SET ? "Write the date and the time to the RTC" : "Digits (pad: Up/Down): type   Left/Right: cursor";
}

static const char *keys() { return "Up/Down: select   Enter: next / set   Esc: back"; }

const ft_page_t ft_page_clock = { open, close, NULL, NULL, draw, key, tick, click, wheel, hint, keys };
