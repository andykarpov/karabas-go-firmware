#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "FT812.h"
#include "usb_hid_keys.h"
#include "hid_driver.h"
#include "hid_learn.h"
#include "ui/ft_mouse.h"
#include "ui/ft_page.h"
#include "app_ft_pages.h"

// The Gamepad page of the graphic setup, as Map USB gamepad of the text menu: find the pad (the
// first one whose report changes), let it rest to learn the idle report and its jitter, then for
// each button wait for a press and for the release. A Mega Drive pad shows the button asked for;
// when all are done it shows the presses through the learned map, also after Save (a test).
// The keyboard and the mouse drive the page: the pad being mapped must not.

enum {
  ST_SELECT,  // waiting for a pad
  ST_SETTLE,  // a pad was found, give it time to be released
  ST_REST,    // collecting the idle reports
  ST_PRESS,   // waiting for the press of button `btn`
  ST_RELEASE, // waiting for the release
  ST_DONE,    // all buttons done, save?
  ST_SAVED,
  ST_FAILED,
};

// the clickable areas
enum { HIT_SKIP, HIT_BACK, HIT_SAVE, HIT_AGAIN };

#define SETTLE_MS 700
#define REST_MS 700
#define PRESS_TIMEOUT_MS 10000
#define RELEASE_TIMEOUT_MS 3000

#define PAD_W 300
#define PAD_H 98
#define BTN_W 110
#define LIST_STEP 16
#define LIST_FONT 26

#define PAD_BODY 0xc8ccd4
#define PAD_EDGE 0x334155
#define PAD_KEY 0x475569
#define PAD_LABEL 0xffffff
#define PAD_LABEL_OFF 0x94a3b8

static const char *const btn_names[HID_JOY_BUTTONS] = {"Up", "Down", "Left", "Right", "A", "B", "C", "X", "Y", "Z", "Start", "Mode"};
enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_A, B_B, B_C, B_X, B_Y, B_Z, B_START, B_MODE };

static uint8_t state = ST_SELECT;
static uint8_t btn;
static ElapsedTimer timer;
static uint32_t last_seq;
static hid_capture_t rep;
static hid_learn_rest_t rest;
static hid_joy_rule_t rules[HID_JOY_BUTTONS];
static bool mapped[HID_JOY_BUTTONS];
static hid_joy_config_t cfg; // the learned map (ST_DONE and later)
static uint16_t live;        // the buttons held, through cfg
static char saved_path[32];
static bool dirty;

static void set_state(uint8_t s) {
  state = s;
  timer.reset();
  dirty = true;
}

static void build_cfg() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.vid = rep.vid;
  cfg.pid = rep.pid;
  cfg.report_id = rest.report_id ? rest.report_id : -1;
  for (uint8_t i = 0; i < HID_JOY_BUTTONS; i++) {
    if (!mapped[i]) continue;
    cfg.rules[cfg.count] = rules[i];
    cfg.rules[cfg.count].btn = hid_joy_btns[i];
    cfg.count++;
  }
  live = 0;
}

static void start() {
  btn = 0;
  last_seq = 0;
  live = 0;
  memset(mapped, 0, sizeof(mapped));
  hid_learn_rest_reset(&rest);
  hid_capture_start();
  set_state(ST_SELECT);
}

static void open() { start(); }

static void close() { hid_capture_stop(); }

// ---------------- drawing ----------------

// lit: asked for or held
static uint32_t key_color(const ft_page_ctx_t &c, bool lit) { return lit ? c.s->color_active : PAD_KEY; }

static bool lit(uint8_t i) {
  if (state == ST_PRESS || state == ST_RELEASE) return i == btn;
  if (state >= ST_DONE) return live & hid_joy_btns[i];
  return false;
}

static uint32_t label_color(const ft_page_ctx_t &c, uint8_t i) {
  if (lit(i)) return c.s->color_text_active;
  return state >= ST_PRESS && !mapped[i] && (state >= ST_DONE || i < btn) ? PAD_LABEL_OFF : PAD_LABEL;
}

// the pad is drawn in units of a 400 x 130 picture, at 3/4
#define K(v) ((v) * 3 / 4)

static void draw_round(const ft_page_ctx_t &c, int16_t x, int16_t y, uint8_t r, uint8_t i) {
  ft.drawCircle(x, y, K(r), key_color(c, lit(i)));
  ft.drawText(x, y, r > 14 ? 27 : 26, label_color(c, i), FT81x_OPT_CENTER, btn_names[i]);
}

static void draw_small(const ft_page_ctx_t &c, int16_t x, int16_t y, uint8_t i, const char *label) {
  ft_page_rect(x, y, K(60), K(18), 6, key_color(c, lit(i)));
  ft.drawText(x + K(30), y + K(9), 20, label_color(c, i), FT81x_OPT_CENTER, label);
}

static void draw_key(const ft_page_ctx_t &c, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t i) {
  ft_page_rect(x, y, w, h, 2, key_color(c, lit(i)));
}

// a Mega Drive pad, PAD_W x PAD_H
static void draw_pad(const ft_page_ctx_t &c, int16_t px, int16_t py) {
  ft_page_rect(px, py, PAD_W, PAD_H, K(30), PAD_EDGE);
  ft_page_rect(px + 3, py + 3, PAD_W - 6, PAD_H - 6, K(30) - 3, PAD_BODY);

  int16_t cx = px + K(80), cy = py + PAD_H / 2;
  draw_key(c, cx - K(12), cy - K(42), K(24), K(30), B_UP);
  draw_key(c, cx - K(12), cy + K(12), K(24), K(30), B_DOWN);
  draw_key(c, cx - K(42), cy - K(12), K(30), K(24), B_LEFT);
  draw_key(c, cx + K(12), cy - K(12), K(30), K(24), B_RIGHT);
  ft.drawRect(cx - K(12), cy - K(12), K(24), K(24), 0, PAD_EDGE);

  draw_small(c, px + K(170), py + K(34), B_MODE, "MODE");
  draw_small(c, px + K(170), py + K(70), B_START, "START");

  draw_round(c, px + K(282), py + K(42), 13, B_X);
  draw_round(c, px + K(316), py + K(36), 13, B_Y);
  draw_round(c, px + K(350), py + K(30), 13, B_Z);
  draw_round(c, px + K(272), py + K(90), 17, B_A);
  draw_round(c, px + K(314), py + K(82), 17, B_B);
  draw_round(c, px + K(356), py + K(74), 17, B_C);
}

// the learned sources, two columns
static void draw_rules(const ft_page_ctx_t &c, int16_t y) {
  int16_t col = (c.w - 32) / 2;
  for (uint8_t i = 0; i < HID_JOY_BUTTONS; i++) {
    int16_t x = c.x + 16 + (i / 6) * col, ry = y + (i % 6) * LIST_STEP + LIST_STEP / 2;
    bool current = (state == ST_PRESS || state == ST_RELEASE) && i == btn;
    uint32_t color = current ? c.s->color_active : c.s->color_text;
    ft.drawText(x, ry, LIST_FONT, color, FT81x_OPT_CENTERY, btn_names[i]);
    char b[24] = "";
    if (mapped[i]) hid_learn_format_rule(&rules[i], b, sizeof(b));
    else if (state >= ST_PRESS && (i < btn || state >= ST_DONE)) strcpy(b, "-");
    ft.drawText(x + 56, ry, LIST_FONT, color, FT81x_OPT_CENTERY, b);
  }
}

static void draw(const ft_page_ctx_t &c) {
  uint32_t text = c.s->color_text, dim = c.s->color_copyright;
  int16_t x = c.x + 16;
  char line1[40] = "", line2[40] = "";
  uint32_t color1 = text;

  if (!c.focused) {
    strcpy(line1, "Map a USB pad to the 12 buttons");
    strcpy(line2, "Enter: start");
  } else {
    switch (state) {
      case ST_SELECT:
        strcpy(line1, "Press a button on the USB pad");
        break;
      case ST_SETTLE:
      case ST_REST:
        snprintf(line1, sizeof(line1), "Pad %04x:%04x", rep.vid, rep.pid);
        strcpy(line2, "Release all, don't touch it");
        break;
      case ST_PRESS:
        snprintf(line1, sizeof(line1), "Press %s", btn_names[btn]);
        snprintf(line2, sizeof(line2), "Skipped in %lu s", (unsigned long) ((PRESS_TIMEOUT_MS - min((uint32_t) timer.elapsed(), (uint32_t) PRESS_TIMEOUT_MS) + 999) / 1000));
        break;
      case ST_RELEASE:
        snprintf(line1, sizeof(line1), "Release %s", btn_names[btn]);
        break;
      case ST_DONE:
        strcpy(line1, "All done");
        strcpy(line2, "Try the pad, then save");
        break;
      case ST_SAVED:
        strcpy(line1, "Saved");
        color1 = FT_PAGE_OK;
        strlcpy(line2, saved_path, sizeof(line2));
        break;
      case ST_FAILED:
        strcpy(line1, "Not saved");
        color1 = FT_PAGE_ERROR;
        strcpy(line2, "Could not write the SD card");
        break;
    }
  }
  ft.drawText(x, c.y + FT_PAGE_BTN_H / 2, FT_PAGE_FONT, color1, FT81x_OPT_CENTERY, line1);
  ft.drawText(x, c.y + FT_PAGE_BTN_H + 12, FT_PAGE_FONT_SMALL, dim, FT81x_OPT_CENTERY, line2);

  // the buttons for the mouse, right of the first line
  int16_t bx = c.x + c.w - 16 - BTN_W, bx2 = bx - BTN_W - 8;
  if (c.focused) {
    if (state == ST_PRESS) {
      ft_page_button(c, bx, c.y, BTN_W, "Skip", true, HIT_SKIP);
      if (btn > 0) ft_page_button(c, bx2, c.y, BTN_W, "Back", false, HIT_BACK);
    } else if (state == ST_DONE) {
      ft_page_button(c, bx, c.y, BTN_W, "Save", true, HIT_SAVE);
      ft_page_button(c, bx2, c.y, BTN_W, "Back", false, HIT_BACK);
    } else if (state == ST_SAVED || state == ST_FAILED) {
      ft_page_button(c, bx, c.y, BTN_W, "Again", true, HIT_AGAIN);
    }
  }

  int16_t py = c.y + FT_PAGE_BTN_H + 28;
  int16_t pw = c.w - 32 < PAD_W ? c.w - 32 : PAD_W;
  draw_pad(c, c.x + 16 + (c.w - 32 - pw) / 2, py);
  if (c.focused) draw_rules(c, py + PAD_H + 10);
}

// ---------------- steps ----------------

static void next_button() {
  btn++;
  if (btn < HID_JOY_BUTTONS) {
    set_state(ST_PRESS);
  } else {
    build_cfg();
    set_state(ST_DONE);
  }
}

static void prev_button() {
  if (btn == 0) return;
  btn--;
  mapped[btn] = false;
  set_state(ST_PRESS);
}

static void save() {
  bool any = false;
  for (uint8_t i = 0; i < HID_JOY_BUTTONS; i++) any |= mapped[i];
  if (!any) return;
  build_cfg();
  // the capture goes on: the pad keeps showing the presses
  set_state(hid_driver_save(&cfg, saved_path, sizeof(saved_path)) ? ST_SAVED : ST_FAILED);
}

static bool tick() {
  bool fresh = hid_capture_get(&rep) && rep.seq != last_seq;
  if (fresh) last_seq = rep.seq;

  switch (state) {
    case ST_SELECT:
      if (fresh) set_state(ST_SETTLE);
      break;
    case ST_SETTLE:
      if (timer.elapsed() >= SETTLE_MS) { hid_learn_rest_reset(&rest); state = ST_REST; timer.reset(); }
      break;
    case ST_REST:
      if (fresh) hid_learn_rest_add(&rest, rep.report_id, rep.data, rep.len);
      // a pad may only report on changes: the last report is the idle one then
      if (timer.elapsed() >= REST_MS) {
        if (rest.len == 0) hid_learn_rest_add(&rest, rep.report_id, rep.data, rep.len);
        btn = 0;
        set_state(ST_PRESS);
      }
      break;
    case ST_PRESS:
      if (fresh && hid_learn_detect(&rest, rep.report_id, rep.data, rep.len, &rules[btn])) {
        mapped[btn] = true;
        set_state(ST_RELEASE);
      } else if (timer.elapsed() >= PRESS_TIMEOUT_MS) {
        next_button(); // skip
      }
      break;
    case ST_RELEASE:
      if ((fresh && hid_learn_is_rest(&rest, rep.report_id, rep.data, rep.len)) || timer.elapsed() >= RELEASE_TIMEOUT_MS) {
        next_button();
      }
      break;
    default: // the test: the presses through the learned map
      if (fresh && hid_joy_accepts_report(&cfg, rep.report_id)) {
        uint16_t held = hid_joy_map_report(&cfg, rep.data, rep.len, 0);
        if (held != live) { live = held; dirty = true; }
      }
  }
  bool d = dirty;
  dirty = false;
  return d;
}

static bool key(uint8_t key, bool shift, bool joy) {
  if (joy) return true; // it may be the pad being mapped
  if (key == KEY_ESC) return false;
  bool enter = key == KEY_ENTER || key == KEY_KPENTER;
  if (enter && state == ST_PRESS) next_button(); // skip
  else if (enter && state == ST_DONE) save();
  else if (enter && (state == ST_SAVED || state == ST_FAILED)) start();
  else if (key == KEY_BACKSPACE && (state == ST_PRESS || state == ST_RELEASE || state == ST_DONE)) prev_button();
  dirty = false; // the setup draws after a key
  return true;
}

static bool click(int16_t id) {
  switch (id) {
    case HIT_SKIP: if (state == ST_PRESS) next_button(); break;
    case HIT_BACK: prev_button(); break;
    case HIT_SAVE: save(); break;
    case HIT_AGAIN: start(); break;
  }
  dirty = false;
  return true;
}

static const char *hint() {
  switch (state) {
    case ST_PRESS: return "Enter: skip   Backspace: back";
    case ST_DONE: return "Enter: save the driver   Backspace: redo the last one";
    case ST_SAVED: case ST_FAILED: return "Enter: map again";
    default: return "";
  }
}

static const char *keys() { return "Esc: back (the keyboard or the mouse, not the pad)"; }

const ft_page_t ft_page_pad = { open, close, nullptr, nullptr, draw, key, tick, click, nullptr, hint, keys };
