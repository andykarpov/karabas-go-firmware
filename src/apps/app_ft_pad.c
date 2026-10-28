#include "config.h"
#include "types.h"
#include "main.h"
#include "ft812.h"
#include "input/usb_hid_keys.h"
#include "usb/hid_driver.h"
#include "hid_learn.h"
#include "apps/pad_learn.h"
#include "ui/ft_mouse.h"
#include "ui/ft_page.h"
#include "apps/app_ft_pages.h"

// The Gamepad page of the graphic setup, as Map USB gamepad of the text menu. First the two
// joystick ports: the pad on each and what maps it (a driver ini, the default map, XInput);
// Forget renames the driver ini of a pad to .bak. Map learns a pad through apps/pad_learn
// (shared with the text menu). A Mega Drive pad shows the button asked for and the ones mapped;
// when all are done it shows the presses through the learned map, also after Save (a test).
// Test: both ports as the core gets them (the USB pads and the Sega / DB9 ones of the board),
// until Esc or a hold of Start+Mode; the global joystick hotkeys are off meanwhile.
// The learned sources are not listed as in the text menu: 24 more texts are too much for the
// FT812 in 800x600@60 (40 MHz), the lower lines of the screen are left out.
// The keyboard and the mouse drive the page: the pad being mapped must not.

enum {
  V_LIST,    // the ports
  V_TEST,    // the presses on both ports
  V_MAPPING, // pad_learn runs
};

// the clickable areas
enum { HIT_SKIP, HIT_BACK, HIT_SAVE, HIT_DONE, HIT_MAP, HIT_TEST, HIT_FORGET, HIT_PORT = 10 };

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

enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_A, B_B, B_C, B_X, B_Y, B_Z, B_START, B_MODE };

static uint8_t view = V_LIST;
static uint32_t timer = 0;
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

static void set_view(uint8_t v) {
  view = v;
  timer = ms_now();
  dirty = true;
}

static bool can_forget() { return pads[port].source == USB_PAD_DRIVER; }
static uint8_t list_btns() { return can_forget() ? 3 : 2; }

static void show_list() {
  pl_stop();
  usb_pads_get(pads);
  list_btn = 0;
  set_view(V_LIST);
}

static void start_test() {
  test_joy[0] = test_joy[1] = 0;
  list_msg = NULL;
  test_exit = ms_now();
  set_view(V_TEST);
}

static void stop_test() {
  joy_released = false; // Start+Mode may still be held: Mode is Esc on the list
  show_list();
  list_btn = 1;
}

static void start() {
  list_msg = NULL;
  pl_start();
  set_view(V_MAPPING);
}

static void page_show() {
  port = 0;
  list_msg = NULL;
  show_list();
}

static void page_hide() { pl_stop(); }

static void open() {
  if (view != V_LIST) show_list();
}

// the keys go back to the tabs (Esc on the list, a click on a tab)
static void close() {
  if (view != V_LIST) show_list();
}

// ---------------- drawing ----------------

// lit: asked for or held
static uint32_t key_color(const ft_page_ctx_t *c, bool lit) { return lit ? c->s->color_active : PAD_KEY; }

static bool lit(uint8_t i) {
  if (view == V_TEST) return test_joy[draw_port] & hid_joy_btns[i];
  uint8_t st = pl_state();
  if (st == PL_PRESS || st == PL_RELEASE) return i == pl_btn();
  if (st >= PL_DONE) return pl_live() & hid_joy_btns[i];
  return false;
}

static uint32_t label_color(const ft_page_ctx_t *c, uint8_t i) {
  if (lit(i)) return c->s->color_text_active;
  uint8_t st = view == V_MAPPING ? pl_state() : PL_IDLE;
  return st >= PL_PRESS && !pl_mapped(i) && (st >= PL_DONE || i < pl_btn()) ? PAD_LABEL_OFF : PAD_LABEL;
}

// the pad is drawn in units of a 400 x 130 picture, at 3/4
#define K(v) ((v) * 3 / 4)

static void draw_round(const ft_page_ctx_t *c, int16_t x, int16_t y, uint8_t r, uint8_t i) {
  ft_draw_circle(x, y, K(r), key_color(c, lit(i)));
  ft_draw_text(x, y, r > 14 ? 27 : 26, label_color(c, i), FT81x_OPT_CENTER, pl_btn_names[i]);
}

static void draw_small(const ft_page_ctx_t *c, int16_t x, int16_t y, uint8_t i, const char *label) {
  ft_page_rect(x, y, K(60), K(18), 6, key_color(c, lit(i)));
  ft_draw_text(x + K(30), y + K(9), 20, label_color(c, i), FT81x_OPT_CENTER, label);
}

static void draw_key(const ft_page_ctx_t *c, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t i) {
  ft_page_rect(x, y, w, h, 2, key_color(c, lit(i)));
}

// a Mega Drive pad, PAD_W x PAD_H
static void draw_pad(const ft_page_ctx_t *c, int16_t px, int16_t py) {
  ft_page_rect(px, py, PAD_W, PAD_H, K(30), PAD_EDGE);
  ft_page_rect(px + 3, py + 3, PAD_W - 6, PAD_H - 6, K(30) - 3, PAD_BODY);

  int16_t cx = px + K(80), cy = py + PAD_H / 2;
  draw_key(c, cx - K(12), cy - K(42), K(24), K(30), B_UP);
  draw_key(c, cx - K(12), cy + K(12), K(24), K(30), B_DOWN);
  draw_key(c, cx - K(42), cy - K(12), K(30), K(24), B_LEFT);
  draw_key(c, cx + K(12), cy - K(12), K(30), K(24), B_RIGHT);
  ft_draw_rect(cx - K(12), cy - K(12), K(24), K(24), 0, PAD_EDGE);

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
static void pad_source(const usb_pad_t *p, char *out, size_t size) {
  switch (p->source) {
    case USB_PAD_DRIVER: strlcpy(out, p->file, size); break;
    case USB_PAD_DEFAULT: strlcpy(out, "default map", size); break;
    case USB_PAD_XINPUT: strlcpy(out, "XInput", size); break;
    default: strlcpy(out, "no map", size);
  }
}

static void draw_list(const ft_page_ctx_t *c) {
  int16_t x = c->x + 16;
  uint32_t dim = c->s->color_copyright;
  int16_t y = c->y + 4;
  ft_draw_text(x, y + ROW_H / 2, FT_PAGE_FONT_SMALL, dim, FT81x_OPT_CENTERY, "Port");
  ft_draw_text(x + COL_PAD, y + ROW_H / 2, FT_PAGE_FONT_SMALL, dim, FT81x_OPT_CENTERY, "Pad");
  ft_draw_text(x + COL_SRC, y + ROW_H / 2, FT_PAGE_FONT_SMALL, dim, FT81x_OPT_CENTERY, "Mapped by");
  for (uint8_t i = 0; i < 2; i++) {
    const usb_pad_t *p = &pads[i];
    int16_t ry = y + ROW_H * (i + 1);
    bool hovered = c->focused && ft_page_hit(c->x, ry, c->w, ROW_H - 2, HIT_PORT + i);
    ft_page_row(c, c->x, ry, c->w, ROW_H - 2, i == port, hovered);
    int16_t cy = ry + (ROW_H - 2) / 2;
    char b[32];
    snprintf(b, sizeof(b), "%u", i + 1);
    ft_draw_text(x + 8, cy, FT_PAGE_FONT, c->s->color_text, FT81x_OPT_CENTERY, b);
    if (p->source == USB_PAD_NONE && p->vid == 0) {
      ft_draw_text(x + COL_PAD, cy, FT_PAGE_FONT, dim, FT81x_OPT_CENTERY, "-");
      continue;
    }
    snprintf(b, sizeof(b), "%04x:%04x", p->vid, p->pid);
    ft_draw_text(x + COL_PAD, cy, FT_PAGE_FONT, c->s->color_text, FT81x_OPT_CENTERY, b);
    pad_source(p, b, sizeof(b));
    ft_page_text(x + COL_SRC, cy, FT_PAGE_FONT, p->source == USB_PAD_DRIVER ? c->s->color_text : dim,
      FT81x_OPT_CENTERY, b, (c->w - COL_SRC - 32) / 12);
  }
  int16_t by = y + ROW_H * 3 + 12;
  ft_page_button(c, x, by, LIST_BTN_W, "Map", list_btn == 0, HIT_MAP);
  ft_page_button(c, x + LIST_BTN_W + 12, by, LIST_BTN_W, "Test", list_btn == 1, HIT_TEST);
  if (can_forget()) ft_page_button(c, x + 2 * (LIST_BTN_W + 12), by, LIST_BTN_W, "Forget", list_btn == 2, HIT_FORGET);
  if (list_msg && c->focused) {
    ft_draw_text(x, by + FT_PAGE_BTN_H + 20, FT_PAGE_FONT, list_msg_ok ? FT_PAGE_OK : FT_PAGE_ERROR, FT81x_OPT_CENTERY, list_msg);
  }
}

static void draw_mapper(const ft_page_ctx_t *c) {
  uint32_t text = c->s->color_text, dim = c->s->color_copyright;
  int16_t x = c->x + 16;
  char line1[40] = "", line2[40] = "";
  uint32_t color1 = text;

  uint8_t st = pl_state();
  switch (st) {
    case PL_SELECT:
      strcpy(line1, "Press a button on the USB pad");
      break;
    case PL_SETTLE:
    case PL_REST:
      snprintf(line1, sizeof(line1), "Pad %04x:%04x", pl_vid(), pl_pid());
      strcpy(line2, "Release all, don't touch it");
      break;
    case PL_PRESS:
      snprintf(line1, sizeof(line1), "Press %s", pl_btn_names[pl_btn()]);
      snprintf(line2, sizeof(line2), "Skipped in %lu s", (unsigned long) pl_press_left_s());
      break;
    case PL_RELEASE:
      snprintf(line1, sizeof(line1), "Release %s", pl_btn_names[pl_btn()]);
      break;
    case PL_DONE:
      strcpy(line1, "All done");
      strcpy(line2, "Try the pad, then save");
      break;
    case PL_SAVED:
      strcpy(line1, "Saved");
      color1 = FT_PAGE_OK;
      strlcpy(line2, pl_saved_path(), sizeof(line2));
      break;
    case PL_FAILED:
      strcpy(line1, "Not saved");
      color1 = FT_PAGE_ERROR;
      strcpy(line2, "Could not write the SD card");
      break;
  }
  ft_draw_text(x, c->y + FT_PAGE_BTN_H / 2, FT_PAGE_FONT, color1, FT81x_OPT_CENTERY, line1);
  ft_draw_text(x, c->y + FT_PAGE_BTN_H + 12, FT_PAGE_FONT_SMALL, dim, FT81x_OPT_CENTERY, line2);

  // the buttons for the mouse, right of the first line
  int16_t bx = c->x + c->w - 16 - BTN_W, bx2 = bx - BTN_W - 8;
  if (st == PL_PRESS) {
    ft_page_button(c, bx, c->y, BTN_W, "Skip", true, HIT_SKIP);
    if (pl_btn() > 0) ft_page_button(c, bx2, c->y, BTN_W, "Back", false, HIT_BACK);
  } else if (st == PL_DONE) {
    ft_page_button(c, bx, c->y, BTN_W, "Save", true, HIT_SAVE);
    ft_page_button(c, bx2, c->y, BTN_W, "Back", false, HIT_BACK);
  } else if (st == PL_SAVED || st == PL_FAILED) {
    ft_page_button(c, bx, c->y, BTN_W, "Done", true, HIT_DONE);
  }

  int16_t py = c->y + FT_PAGE_BTN_H + 28;
  int16_t pw = c->w - 32 < PAD_W ? c->w - 32 : PAD_W;
  draw_pad(c, c->x + 16 + (c->w - 32 - pw) / 2, py);
  int8_t last = pl_last_btn();
  if (last >= 0 && st >= PL_PRESS && st <= PL_DONE) {
    char b[40], r[24] = "-";
    if (pl_mapped(last)) hid_learn_format_rule(pl_rule(last), r, sizeof(r));
    snprintf(b, sizeof(b), "%s = %s", pl_btn_names[last], r);
    ft_draw_text(x, py + PAD_H + 16, FT_PAGE_FONT_SMALL, dim, FT81x_OPT_CENTERY, b);
  }
}

// the two ports, one pad under the other
static void draw_test(const ft_page_ctx_t *c) {
  int16_t pw = c->w - 32 < PAD_W ? c->w - 32 : PAD_W;
  int16_t px = c->x + 16 + (c->w - 32 - pw) / 2;
  for (uint8_t i = 0; i < 2; i++) {
    const usb_pad_t *p = &pads[i];
    int16_t y = c->y + i * (TEST_LABEL_H + PAD_H + 10);
    char src[24], b[48];
    if (p->source == USB_PAD_NONE && p->vid == 0) {
      snprintf(b, sizeof(b), "Port %u: no USB pad", i + 1);
    } else {
      pad_source(p, src, sizeof(src));
      snprintf(b, sizeof(b), "Port %u: %04x:%04x, %s", i + 1, p->vid, p->pid, src);
    }
    ft_page_text(px, y + TEST_LABEL_H / 2 - 2, FT_PAGE_FONT_SMALL, c->s->color_text, FT81x_OPT_CENTERY, b, pw / 10);
    draw_port = i;
    draw_pad(c, px, y + TEST_LABEL_H);
  }
}

static void draw(const ft_page_ctx_t *c) {
  if (view == V_LIST) draw_list(c);
  else if (view == V_TEST) draw_test(c);
  else draw_mapper(c);
}

// ---------------- steps ----------------

static void forget() {
  bool ok = hid_driver_forget(pads[port].vid, pads[port].pid);
  list_msg = ok ? "The driver is forgotten (.bak)" : "Could not rename the driver";
  list_msg_ok = ok;
  usb_pads_get(pads);
  list_btn = 0;
}

static bool tick() {
  if (!joy_released && (joyL | joyR) == 0) joy_released = true;
  if (view == V_LIST || view == V_TEST) {
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
  if (view == V_TEST) {
    uint16_t l = joyL, r = joyR;
    if (l != test_joy[0] || r != test_joy[1]) { test_joy[0] = l; test_joy[1] = r; dirty = true; }
    if ((l & TEST_EXIT) != TEST_EXIT && (r & TEST_EXIT) != TEST_EXIT) test_exit = ms_now();
    else if (ms_since(test_exit) >= TEST_EXIT_MS) stop_test();
  } else if (view == V_MAPPING && pl_tick()) {
    dirty = true;
  }
  bool d = dirty;
  dirty = false;
  return d;
}

static bool list_key(uint8_t key) {
  bool enter = key == KEY_ENTER || key == KEY_KPENTER;
  if (key == KEY_ESC) return false;
  if (key == KEY_UP || key == KEY_DOWN) { port ^= 1; list_msg = NULL; }
  else if (key == KEY_RIGHT || key == KEY_TAB) list_btn = (list_btn + 1) % list_btns();
  else if (key == KEY_LEFT) list_btn = (list_btn + list_btns() - 1) % list_btns();
  else if (enter && list_btn == 0) start();
  else if (enter && list_btn == 1) start_test();
  else if (enter) forget();
  if (list_btn >= list_btns()) list_btn = 0;
  return true;
}

bool ft_page_pad_mapping() {
  return view != V_LIST;
}

static bool key(uint8_t key, bool shift, bool joy) {
  if (joy && !joy_released) return true; // the keys of the Start+Mode that ended the test
  if (view == V_LIST) {
    bool r = list_key(key);
    dirty = false; // the setup draws after a key
    return r;
  }
  if (joy) return true; // it may be the pad being mapped
  bool enter = key == KEY_ENTER || key == KEY_KPENTER;
  if (key == KEY_ESC || (view == V_MAPPING && enter && pl_state() >= PL_SAVED)) show_list();
  else if (view != V_MAPPING) {}
  else if (enter && pl_state() == PL_PRESS) pl_skip();
  else if (enter && pl_state() == PL_DONE) pl_save();
  else if (key == KEY_BACKSPACE) pl_back();
  dirty = false;
  return true;
}

static bool click(int16_t id) {
  if (id == HIT_PORT || id == HIT_PORT + 1) {
    if (port != id - HIT_PORT) list_msg = NULL;
    port = id - HIT_PORT;
    if (list_btn >= list_btns()) list_btn = 0;
  } else {
    switch (id) {
      case HIT_MAP: start(); break;
      case HIT_TEST: list_btn = 1; start_test(); break;
      case HIT_FORGET: list_btn = 2; forget(); break;
      case HIT_SKIP: pl_skip(); break;
      case HIT_BACK: pl_back(); break;
      case HIT_SAVE: pl_save(); break;
      case HIT_DONE: show_list(); break;
    }
  }
  dirty = false;
  return true;
}

static const char *hint() {
  if (view == V_TEST) return "Hold Start+Mode for 2 s to go back";
  switch (view == V_LIST ? 0xff : pl_state()) {
    case 0xff: return list_btn == 2 ? "Forget: the driver ini becomes .bak, the default map is used" :
                         list_btn == 1 ? "Test: the buttons held on both ports, as the core gets them" :
                                         "Map: learns the pad you press a button on, also one not on a port";
    case PL_PRESS: return "Enter: skip   Backspace: back";
    case PL_DONE: return "Enter: save the driver   Backspace: redo the last one";
    case PL_SAVED: case PL_FAILED: return "Enter: back to the ports";
    default: return "";
  }
}

static const char *keys() {
  return view == V_LIST ? "Up/Down: port   Left/Right: button   Enter: OK   Esc: back" :
         view == V_TEST ? "Esc: back to the ports (the keyboard), or Start+Mode held on the pad" :
                            "Esc: back to the ports (the keyboard or the mouse, not the pad)";
}

const ft_page_t ft_page_pad = { open, close, page_show, page_hide, draw, key, tick, click, NULL, hint, keys };
