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

// The Gamepad page of the graphic setup, as Map USB gamepad of the text menu. First the two
// joystick ports: the pad on each and what maps it (a driver ini, the default map, XInput);
// Forget renames the driver ini of a pad to .bak. Map: find the pad (the first one whose report
// changes), let it rest to learn the idle report and its jitter, then for each button wait for
// a press and for the release. A Mega Drive pad shows the button asked for and the ones mapped;
// when all are done it shows the presses through the learned map, also after Save (a test).
// Test: both ports as the core gets them (the USB pads and the Sega / DB9 ones of the board),
// until Esc or a hold of Start+Mode; the global joystick hotkeys are off meanwhile.
// The learned sources are not listed as in the text menu: 24 more texts are too much for the
// FT812 in 800x600@60 (40 MHz), the lower lines of the screen are left out.
// The keyboard and the mouse drive the page: the pad being mapped must not.

enum {
  ST_LIST,    // the ports
  ST_TEST,    // the presses on both ports
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
enum { HIT_SKIP, HIT_BACK, HIT_SAVE, HIT_DONE, HIT_MAP, HIT_TEST, HIT_FORGET, HIT_PORT = 10 };

#define SETTLE_MS 700
#define REST_MS 700
#define PRESS_TIMEOUT_MS 10000
#define RELEASE_TIMEOUT_MS 3000
#define LIST_REFRESH_MS 500
#define TEST_EXIT_MS 2000
#define TEST_EXIT (SC_BTN_START | SC_BTN_MODE)

#define PAD_W 300
#define PAD_H 98
#define BTN_W 110
#define LIST_BTN_W 120
#define TEST_LABEL_H 22
#define ROW_H 30
#define COL_PAD 80
#define COL_SRC 220

#define PAD_BODY 0xc8ccd4
#define PAD_EDGE 0x334155
#define PAD_KEY 0x475569
#define PAD_LABEL 0xffffff
#define PAD_LABEL_OFF 0x94a3b8

static const char *const btn_names[HID_JOY_BUTTONS] = {"Up", "Down", "Left", "Right", "A", "B", "C", "X", "Y", "Z", "Start", "Mode"};
enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_A, B_B, B_C, B_X, B_Y, B_Z, B_START, B_MODE };

static uint8_t state = ST_LIST;
static uint8_t btn;
static int8_t last_btn = -1; // the button learned last, shown under the pad
static uint32_t timer = 0;
static uint32_t last_seq;
static hid_capture_t rep;
static hid_learn_rest_t rest;
static hid_joy_rule_t rules[HID_JOY_BUTTONS];
static bool mapped[HID_JOY_BUTTONS];
static hid_joy_config_t cfg; // the learned map (ST_DONE and later)
static uint16_t live;        // the buttons held, through cfg
static char saved_path[32];
static bool dirty;

static usb_pad_t pads[2];
static uint8_t port;         // the port chosen in the list
static uint8_t list_btn;     // 0: Map, 1: Test, 2: Forget
static const char *list_msg;
static bool list_msg_ok;
static uint16_t test_joy[2]; // the test: the buttons held on each port
static uint8_t draw_port;    // the port whose pad is drawn
static uint32_t test_exit = 0; // how long Start+Mode is held
static bool joy_released = true; // after the test: the pad keys count again once all are released

static void set_state(uint8_t s) {
  state = s;
  timer = ms_now();
  dirty = true;
}

static bool can_forget() { return pads[port].source == USB_PAD_DRIVER; }
static uint8_t list_btns() { return can_forget() ? 3 : 2; }

static void show_list() {
  hid_capture_stop();
  usb_pads_get(pads);
  list_btn = 0;
  set_state(ST_LIST);
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

static void start_test() {
  test_joy[0] = test_joy[1] = 0;
  list_msg = nullptr;
  test_exit = ms_now();
  set_state(ST_TEST);
}

static void stop_test() {
  joy_released = false; // Start+Mode may still be held: Mode is Esc on the list
  show_list();
  list_btn = 1;
}

static void start() {
  btn = 0;
  last_btn = -1;
  last_seq = 0;
  live = 0;
  list_msg = nullptr;
  memset(mapped, 0, sizeof(mapped));
  hid_learn_rest_reset(&rest);
  hid_capture_start();
  set_state(ST_SELECT);
}

static void page_show() {
  port = 0;
  list_msg = nullptr;
  show_list();
}

static void page_hide() { hid_capture_stop(); }

static void open() {
  if (state != ST_LIST) show_list();
}

// the keys go back to the tabs (Esc on the list, a click on a tab)
static void close() {
  if (state != ST_LIST) show_list();
}

// ---------------- drawing ----------------

// lit: asked for or held
static uint32_t key_color(const ft_page_ctx_t &c, bool lit) { return lit ? c.s->color_active : PAD_KEY; }

static bool lit(uint8_t i) {
  if (state == ST_TEST) return test_joy[draw_port] & hid_joy_btns[i];
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

// the source of a pad
static void pad_source(const usb_pad_t &p, char *out, size_t size) {
  switch (p.source) {
    case USB_PAD_DRIVER: strlcpy(out, p.file, size); break;
    case USB_PAD_DEFAULT: strlcpy(out, "default map", size); break;
    case USB_PAD_XINPUT: strlcpy(out, "XInput", size); break;
    default: strlcpy(out, "no map", size);
  }
}

static void draw_list(const ft_page_ctx_t &c) {
  int16_t x = c.x + 16;
  uint32_t dim = c.s->color_copyright;
  int16_t y = c.y + 4;
  ft.drawText(x, y + ROW_H / 2, FT_PAGE_FONT_SMALL, dim, FT81x_OPT_CENTERY, "Port");
  ft.drawText(x + COL_PAD, y + ROW_H / 2, FT_PAGE_FONT_SMALL, dim, FT81x_OPT_CENTERY, "Pad");
  ft.drawText(x + COL_SRC, y + ROW_H / 2, FT_PAGE_FONT_SMALL, dim, FT81x_OPT_CENTERY, "Mapped by");
  for (uint8_t i = 0; i < 2; i++) {
    const usb_pad_t &p = pads[i];
    int16_t ry = y + ROW_H * (i + 1);
    bool hovered = c.focused && ft_page_hit(c.x, ry, c.w, ROW_H - 2, HIT_PORT + i);
    ft_page_row(c, c.x, ry, c.w, ROW_H - 2, i == port, hovered);
    int16_t cy = ry + (ROW_H - 2) / 2;
    char b[32];
    snprintf(b, sizeof(b), "%u", i + 1);
    ft.drawText(x + 8, cy, FT_PAGE_FONT, c.s->color_text, FT81x_OPT_CENTERY, b);
    if (p.source == USB_PAD_NONE && p.vid == 0) {
      ft.drawText(x + COL_PAD, cy, FT_PAGE_FONT, dim, FT81x_OPT_CENTERY, "-");
      continue;
    }
    snprintf(b, sizeof(b), "%04x:%04x", p.vid, p.pid);
    ft.drawText(x + COL_PAD, cy, FT_PAGE_FONT, c.s->color_text, FT81x_OPT_CENTERY, b);
    pad_source(p, b, sizeof(b));
    ft_page_text(x + COL_SRC, cy, FT_PAGE_FONT, p.source == USB_PAD_DRIVER ? c.s->color_text : dim,
      FT81x_OPT_CENTERY, b, (c.w - COL_SRC - 32) / 12);
  }
  int16_t by = y + ROW_H * 3 + 12;
  ft_page_button(c, x, by, LIST_BTN_W, "Map", list_btn == 0, HIT_MAP);
  ft_page_button(c, x + LIST_BTN_W + 12, by, LIST_BTN_W, "Test", list_btn == 1, HIT_TEST);
  if (can_forget()) ft_page_button(c, x + 2 * (LIST_BTN_W + 12), by, LIST_BTN_W, "Forget", list_btn == 2, HIT_FORGET);
  if (list_msg && c.focused) {
    ft.drawText(x, by + FT_PAGE_BTN_H + 20, FT_PAGE_FONT, list_msg_ok ? FT_PAGE_OK : FT_PAGE_ERROR, FT81x_OPT_CENTERY, list_msg);
  }
}

static void draw_mapper(const ft_page_ctx_t &c) {
  uint32_t text = c.s->color_text, dim = c.s->color_copyright;
  int16_t x = c.x + 16;
  char line1[40] = "", line2[40] = "";
  uint32_t color1 = text;

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
      snprintf(line2, sizeof(line2), "Skipped in %lu s", (unsigned long) ((PRESS_TIMEOUT_MS - MIN((uint32_t) ms_since(timer), (uint32_t) PRESS_TIMEOUT_MS) + 999) / 1000));
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
  ft.drawText(x, c.y + FT_PAGE_BTN_H / 2, FT_PAGE_FONT, color1, FT81x_OPT_CENTERY, line1);
  ft.drawText(x, c.y + FT_PAGE_BTN_H + 12, FT_PAGE_FONT_SMALL, dim, FT81x_OPT_CENTERY, line2);

  // the buttons for the mouse, right of the first line
  int16_t bx = c.x + c.w - 16 - BTN_W, bx2 = bx - BTN_W - 8;
  if (state == ST_PRESS) {
    ft_page_button(c, bx, c.y, BTN_W, "Skip", true, HIT_SKIP);
    if (btn > 0) ft_page_button(c, bx2, c.y, BTN_W, "Back", false, HIT_BACK);
  } else if (state == ST_DONE) {
    ft_page_button(c, bx, c.y, BTN_W, "Save", true, HIT_SAVE);
    ft_page_button(c, bx2, c.y, BTN_W, "Back", false, HIT_BACK);
  } else if (state == ST_SAVED || state == ST_FAILED) {
    ft_page_button(c, bx, c.y, BTN_W, "Done", true, HIT_DONE);
  }

  int16_t py = c.y + FT_PAGE_BTN_H + 28;
  int16_t pw = c.w - 32 < PAD_W ? c.w - 32 : PAD_W;
  draw_pad(c, c.x + 16 + (c.w - 32 - pw) / 2, py);
  if (last_btn >= 0 && state >= ST_PRESS && state <= ST_DONE) {
    char b[40], r[24] = "-";
    if (mapped[last_btn]) hid_learn_format_rule(&rules[last_btn], r, sizeof(r));
    snprintf(b, sizeof(b), "%s = %s", btn_names[last_btn], r);
    ft.drawText(x, py + PAD_H + 16, FT_PAGE_FONT_SMALL, dim, FT81x_OPT_CENTERY, b);
  }
}

// the two ports, one pad under the other
static void draw_test(const ft_page_ctx_t &c) {
  int16_t pw = c.w - 32 < PAD_W ? c.w - 32 : PAD_W;
  int16_t px = c.x + 16 + (c.w - 32 - pw) / 2;
  for (uint8_t i = 0; i < 2; i++) {
    const usb_pad_t &p = pads[i];
    int16_t y = c.y + i * (TEST_LABEL_H + PAD_H + 10);
    char src[24], b[48];
    if (p.source == USB_PAD_NONE && p.vid == 0) {
      snprintf(b, sizeof(b), "Port %u: no USB pad", i + 1);
    } else {
      pad_source(p, src, sizeof(src));
      snprintf(b, sizeof(b), "Port %u: %04x:%04x, %s", i + 1, p.vid, p.pid, src);
    }
    ft_page_text(px, y + TEST_LABEL_H / 2 - 2, FT_PAGE_FONT_SMALL, c.s->color_text, FT81x_OPT_CENTERY, b, pw / 10);
    draw_port = i;
    draw_pad(c, px, y + TEST_LABEL_H);
  }
}

static void draw(const ft_page_ctx_t &c) {
  if (state == ST_LIST) draw_list(c);
  else if (state == ST_TEST) draw_test(c);
  else draw_mapper(c);
}

// ---------------- steps ----------------

static void next_button() {
  last_btn = btn;
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
  last_btn = btn > 0 ? btn - 1 : -1;
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

static void forget() {
  bool ok = hid_driver_forget(pads[port].vid, pads[port].pid);
  list_msg = ok ? "The driver is forgotten (.bak)" : "Could not rename the driver";
  list_msg_ok = ok;
  usb_pads_get(pads);
  list_btn = 0;
}

static bool tick() {
  if (!joy_released && (joyL | joyR) == 0) joy_released = true;
  if (state == ST_LIST || state == ST_TEST) {
    // the pads come and go
    if (ms_since(timer) >= LIST_REFRESH_MS) {
      timer = ms_now();
      usb_pad_t now[2];
      usb_pads_get(now);
      if (memcmp(now, pads, sizeof(now)) != 0) {
        memcpy(pads, now, sizeof(now));
        if (list_btn >= list_btns()) list_btn = 0;
        dirty = true;
      }
    }
  }
  if (state == ST_TEST) {
    uint16_t l = joyL, r = joyR;
    if (l != test_joy[0] || r != test_joy[1]) { test_joy[0] = l; test_joy[1] = r; dirty = true; }
    if ((l & TEST_EXIT) != TEST_EXIT && (r & TEST_EXIT) != TEST_EXIT) test_exit = ms_now();
    else if (ms_since(test_exit) >= TEST_EXIT_MS) stop_test();
  } else if (state != ST_LIST) {
    bool fresh = hid_capture_get(&rep) && rep.seq != last_seq;
    if (fresh) last_seq = rep.seq;

    switch (state) {
      case ST_SELECT:
        if (fresh) set_state(ST_SETTLE);
        break;
      case ST_SETTLE:
        if (ms_since(timer) >= SETTLE_MS) { hid_learn_rest_reset(&rest); state = ST_REST; timer = ms_now(); }
        break;
      case ST_REST:
        if (fresh) hid_learn_rest_add(&rest, rep.report_id, rep.data, rep.len);
        // a pad may only report on changes: the last report is the idle one then
        if (ms_since(timer) >= REST_MS) {
          if (rest.len == 0) hid_learn_rest_add(&rest, rep.report_id, rep.data, rep.len);
          btn = 0;
          set_state(ST_PRESS);
        }
        break;
      case ST_PRESS:
        if (fresh && hid_learn_detect(&rest, rep.report_id, rep.data, rep.len, &rules[btn])) {
          mapped[btn] = true;
          last_btn = btn;
          set_state(ST_RELEASE);
        } else if (ms_since(timer) >= PRESS_TIMEOUT_MS) {
          next_button(); // skip
        }
        break;
      case ST_RELEASE:
        if ((fresh && hid_learn_is_rest(&rest, rep.report_id, rep.data, rep.len)) || ms_since(timer) >= RELEASE_TIMEOUT_MS) {
          next_button();
        }
        break;
      default: // the test: the presses through the learned map
        if (fresh && hid_joy_accepts_report(&cfg, rep.report_id)) {
          uint16_t held = hid_joy_map_report(&cfg, rep.data, rep.len, 0);
          if (held != live) { live = held; dirty = true; }
        }
    }
  }
  bool d = dirty;
  dirty = false;
  return d;
}

static bool list_key(uint8_t key) {
  bool enter = key == KEY_ENTER || key == KEY_KPENTER;
  if (key == KEY_ESC) return false;
  if (key == KEY_UP || key == KEY_DOWN) { port ^= 1; list_msg = nullptr; }
  else if (key == KEY_RIGHT || key == KEY_TAB) list_btn = (list_btn + 1) % list_btns();
  else if (key == KEY_LEFT) list_btn = (list_btn + list_btns() - 1) % list_btns();
  else if (enter && list_btn == 0) start();
  else if (enter && list_btn == 1) start_test();
  else if (enter) forget();
  if (list_btn >= list_btns()) list_btn = 0;
  return true;
}

bool ft_page_pad_mapping() {
  return state != ST_LIST;
}

static bool key(uint8_t key, bool shift, bool joy) {
  if (joy && !joy_released) return true; // the keys of the Start+Mode that ended the test
  if (state == ST_LIST) {
    bool r = list_key(key);
    dirty = false; // the setup draws after a key
    return r;
  }
  if (joy) return true; // it may be the pad being mapped
  bool enter = key == KEY_ENTER || key == KEY_KPENTER;
  if (key == KEY_ESC || (enter && state >= ST_SAVED)) show_list();
  else if (enter && state == ST_PRESS) next_button(); // skip
  else if (enter && state == ST_DONE) save();
  else if (key == KEY_BACKSPACE && (state == ST_PRESS || state == ST_RELEASE || state == ST_DONE)) prev_button();
  dirty = false;
  return true;
}

static bool click(int16_t id) {
  if (id == HIT_PORT || id == HIT_PORT + 1) {
    if (port != id - HIT_PORT) list_msg = nullptr;
    port = id - HIT_PORT;
    if (list_btn >= list_btns()) list_btn = 0;
  } else {
    switch (id) {
      case HIT_MAP: start(); break;
      case HIT_TEST: list_btn = 1; start_test(); break;
      case HIT_FORGET: list_btn = 2; forget(); break;
      case HIT_SKIP: if (state == ST_PRESS) next_button(); break;
      case HIT_BACK: prev_button(); break;
      case HIT_SAVE: save(); break;
      case HIT_DONE: show_list(); break;
    }
  }
  dirty = false;
  return true;
}

static const char *hint() {
  switch (state) {
    case ST_LIST: return list_btn == 2 ? "Forget: the driver ini becomes .bak, the default map is used" :
                         list_btn == 1 ? "Test: the buttons held on both ports, as the core gets them" :
                                         "Map: learns the pad you press a button on, also one not on a port";
    case ST_TEST: return "Hold Start+Mode for 2 s to go back";
    case ST_PRESS: return "Enter: skip   Backspace: back";
    case ST_DONE: return "Enter: save the driver   Backspace: redo the last one";
    case ST_SAVED: case ST_FAILED: return "Enter: back to the ports";
    default: return "";
  }
}

static const char *keys() {
  return state == ST_LIST ? "Up/Down: port   Left/Right: button   Enter: OK   Esc: back" :
         state == ST_TEST ? "Esc: back to the ports (the keyboard), or Start+Mode held on the pad" :
                            "Esc: back to the ports (the keyboard or the mouse, not the pad)";
}

const ft_page_t ft_page_pad = { open, close, page_show, page_hide, draw, key, tick, click, nullptr, hint, keys };
