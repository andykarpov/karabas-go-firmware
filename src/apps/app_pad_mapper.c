#include "config.h"
#include "types.h"
#include "main.h"
#include "apps/app_setup.h"
#include "osd.h"
#include "input/usb_hid_keys.h"
#include "usb/hid_driver.h"
#include "hid_learn.h"
#include "apps/pad_learn.h"
#include "apps/app_pad_mapper.h"
#include "ui/ui_draw.h"
#include "ui/osd2.h"
#include "ui/ui2.h"

// First the two joystick ports: the pad on each and what maps it (a driver ini, the default
// map, XInput); Forget renames the driver ini of a pad to .bak. Assign learns a pad through
// apps/pad_learn (shared with the Gamepad page of the graphic setup); this screen draws it.
// The keyboard drives the screen: the pad being mapped must not.

enum {
  V_IDLE,    // not on screen
  V_LIST,    // the ports
  V_MAPPING, // pad_learn runs
};

#define LIST_REFRESH_MS 500
#define BTN_WIDTH 10 // Assign and Forget: even lengths, centered exactly

static uint8_t view = V_IDLE;
static uint32_t list_timer = 0;
static usb_pad_t pads[2];
static uint8_t port;       // the port chosen in the list
static uint8_t list_btn;   // 0: Assign, 1: Forget
static bool on_btns;       // the focus: the buttons (else the ports)
static const char *list_msg;
static bool list_msg_ok;

static void draw_status(const char *line1, const char *line2) {
  ui_clear_rows(8, 9);
  osd_set_color(OSD_COLOR_YELLOW_I, OSD_COLOR_BLACK);
  osd_set_pos(0, 8); osd_print(line1);
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_set_pos(0, 9); osd_print(line2);
}

static void draw_buttons() {
  uint8_t st = pl_state();
  for (uint8_t i = 0; i < HID_JOY_BUTTONS; i++) {
    bool current = (st == PL_PRESS || st == PL_RELEASE) && i == pl_btn();
    osd_set_color(current ? OSD_COLOR_BLACK : OSD_COLOR_WHITE, current ? OSD_COLOR_WHITE : OSD_COLOR_BLACK);
    osd_fill(0, 11 + i, UI_COL_LAST, 11 + i, ' ');
    osd_set_pos(0, 11 + i); osd_print(pl_btn_names[i]);
    osd_set_pos(7, 11 + i);
    if (pl_mapped(i)) {
      char buf[24];
      hid_learn_format_rule(pl_rule(i), buf, sizeof(buf));
      osd_print(buf);
    } else if (i < pl_btn() || st >= PL_DONE) {
      osd_print("-");
    }
  }
}

static bool can_forget() { return pads[port].source == USB_PAD_DRIVER; }

// the source of a pad, at most 17 chars
static void pad_source(const usb_pad_t *p, char *out, size_t size) {
  switch (p->source) {
    case USB_PAD_DRIVER: snprintf(out, size, "%.17s", p->file); break;
    case USB_PAD_DEFAULT: strlcpy(out, "default map", size); break;
    case USB_PAD_XINPUT: strlcpy(out, "XInput", size); break;
    default: strlcpy(out, "no map", size);
  }
}

static void draw_list() {
  ui_clear_rows(8, UI_ROW_BODY_LAST);
  osd_set_color(OSD_COLOR_GREY, OSD_COLOR_BLACK);
  osd_set_pos(0, 8); osd_print("Port Pad       Mapped by");
  for (uint8_t i = 0; i < 2; i++) {
    const usb_pad_t *p = &pads[i];
    bool chosen = i == port;
    osd_set_color(chosen ? OSD_COLOR_BLACK : OSD_COLOR_WHITE, chosen ? OSD_COLOR_WHITE : OSD_COLOR_BLACK);
    osd_fill(0, 10 + i, UI_COL_LAST, 10 + i, ' ');
    osd_set_pos(0, 10 + i);
    char src[18], buf[33];
    if (p->source == USB_PAD_NONE && p->vid == 0) {
      snprintf(buf, sizeof(buf), "%u    -", i + 1);
    } else {
      pad_source(p, src, sizeof(src));
      snprintf(buf, sizeof(buf), "%u    %04x:%04x %s", i + 1, p->vid, p->pid, src);
    }
    osd_print(buf);
  }
  ui_button(0, 14, BTN_WIDTH, "Assign", on_btns && list_btn == 0);
  if (can_forget()) ui_button(BTN_WIDTH + 1, 14, BTN_WIDTH, "Forget", on_btns && list_btn == 1);
  if (list_msg) {
    osd_set_color(list_msg_ok ? OSD_COLOR_GREEN_I : OSD_COLOR_RED_I, OSD_COLOR_BLACK);
    osd_set_pos(0, 16); osd_print(list_msg);
  }
  osd_set_color(OSD_COLOR_GREY, OSD_COLOR_BLACK);
  osd_set_pos(0, 18); osd_print("Assign: learns the pad you press");
  osd_set_pos(0, 19); osd_print("a button on, also one not on a");
  osd_set_pos(0, 20); osd_print("port");
  osd_set_pos(0, 21); osd_print("Forget: the driver ini -> .bak,");
  osd_set_pos(0, 22); osd_print("the pad uses its default map");
  ui_hint("Up/Down: port, then buttons", "Enter: OK, Esc: return");
}

// the bitmap OSD (link v2): the ports or the learning on its rows
static void draw2(void) {
  char buf[48];
  for (uint8_t r = 0; r < UI2_ROWS; r++) ui2_text(r, "", 0);
  if (view == V_LIST) {
    ui2_text(0, "Port  Pad        Mapped by", UI2_TITLE);
    for (uint8_t i = 0; i < 2; i++) {
      const usb_pad_t *p = &pads[i];
      char src[18];
      if (p->source == USB_PAD_NONE && p->vid == 0) snprintf(buf, sizeof(buf), "%u     -", i + 1);
      else { pad_source(p, src, sizeof(src)); snprintf(buf, sizeof(buf), "%u     %04x:%04x  %s", i + 1, p->vid, p->pid, src); }
      ui2_text(1 + i, buf, i == port ? (on_btns ? UI2_MARK : UI2_SEL) : 0); // the port of the buttons: framed
    }
    static const char *const btn[] = { "Assign", "Forget" };
    ui2_buttons(4, btn, can_forget() ? 2 : 1, on_btns ? list_btn : -1);
    if (list_msg) ui2_text(5, list_msg, list_msg_ok ? 0 : UI2_SEL);
    ui2_text(6, "Assign: learns the pad you press a button on", UI2_DIM);
    ui2_text(7, "Forget: the driver ini -> .bak (default map)", UI2_DIM);
    ui2_hint("Up / Down: port, then the buttons", "Enter: OK, Esc: back");
    ui2_done();
    return;
  }
  const char *l1 = "", *l2 = "";
  switch (pl_state()) {
    case PL_SELECT: l1 = "Press a button on the USB pad"; break;
    case PL_SETTLE:
    case PL_REST: snprintf(buf, sizeof(buf), "Pad %04x:%04x", pl_vid(), pl_pid()); l1 = buf; l2 = "Release all, don't touch it"; break;
    case PL_PRESS: snprintf(buf, sizeof(buf), "Press %s", pl_btn_names[pl_btn()]); l1 = buf; l2 = "Enter: skip, Backspace: back"; break;
    case PL_RELEASE: snprintf(buf, sizeof(buf), "Release %s", pl_btn_names[pl_btn()]); l1 = buf; break;
    case PL_DONE: l1 = "Done. Enter: save the driver"; l2 = "Backspace: redo the last one"; break;
    case PL_SAVED: l1 = "Saved"; l2 = pl_saved_path(); break;
    case PL_FAILED: l1 = "Could not write the SD card"; break;
  }
  ui2_text(0, l1, UI2_TITLE);
  // the buttons: a window of 7 rows around the one being learned
  uint8_t st = pl_state();
  int cur = (st == PL_PRESS || st == PL_RELEASE) ? pl_btn() : 0;
  int top = cur - 3;
  if (top > HID_JOY_BUTTONS - 7) top = HID_JOY_BUTTONS - 7;
  if (top < 0) top = 0;
  for (int r = 0; r < 7 && top + r < HID_JOY_BUTTONS; r++) {
    int i = top + r;
    char rule[24] = "";
    if (pl_mapped(i)) hid_learn_format_rule(pl_rule(i), rule, sizeof(rule));
    else if (i < pl_btn() || st >= PL_DONE) strcpy(rule, "-");
    ui2_row(1 + r, pl_btn_names[i], rule, NULL, ((st == PL_PRESS || st == PL_RELEASE) && i == cur) ? UI2_SEL : 0);
  }
  // what to do now (release the pad, the keys of a step, the file saved): the last row, under the
  // buttons (the v2 screens have no hint lines, this one is the step, not a key hint)
  ui2_text(UI2_ROWS - 1, l2[0] ? l2 : (st >= PL_SAVED ? "Enter: back to the ports" : ""), 0);
  ui2_done();
}

static void draw() {
  if (osd2_active()) { draw2(); return; }
  char buf[33];
  if (view == V_LIST) {
    draw_list();
    osd_update();
    return;
  }
  switch (pl_state()) {
    case PL_SELECT:
      draw_status("Press a button on the USB pad", "");
      break;
    case PL_SETTLE:
    case PL_REST:
      snprintf(buf, sizeof(buf), "Pad %04x:%04x", pl_vid(), pl_pid());
      draw_status(buf, "Release all, don't touch it");
      break;
    case PL_PRESS:
      snprintf(buf, sizeof(buf), "Press %s", pl_btn_names[pl_btn()]);
      draw_status(buf, "Enter: skip, Backspace: back");
      break;
    case PL_RELEASE:
      snprintf(buf, sizeof(buf), "Release %s", pl_btn_names[pl_btn()]);
      draw_status(buf, "");
      break;
    case PL_DONE:
      draw_status("Done. Enter: save the driver", "Backspace: redo the last one");
      break;
    case PL_SAVED:
      draw_status("Saved", pl_saved_path());
      break;
    case PL_FAILED:
      draw_status("Could not write the SD card", "");
      break;
  }
  draw_buttons();
  ui_hint(pl_state() >= PL_SAVED ? "Enter: back to the ports" : "", "Esc: back to the ports");
  osd_update();
}

static void show_list() {
  pl_stop();
  usb_pads_get(pads);
  list_btn = 0;
  on_btns = false;
  view = V_LIST;
  list_timer = ms_now();
  ui_clear_body();
  draw();
}

static void start() {
  ui_clear_body();
  view = V_MAPPING;
  pl_start();
}

void app_pad_mapper_overlay() {
  if (osd2_active()) ui2_screen("Map USB gamepad");
  else ui_screen("Map USB gamepad");
  port = 0;
  list_msg = NULL;
  show_list();
}

static void forget() {
  bool ok = hid_driver_forget(pads[port].vid, pads[port].pid);
  list_msg = ok ? "The driver is forgotten" : "Could not rename the driver";
  list_msg_ok = ok;
  usb_pads_get(pads);
  list_btn = 0;
}

// Up / Down: the ports, then the buttons under them; Left / Right / Tab or Enter on a port: to the
// buttons; Enter on a button: it
static void list_on_keyboard(uint8_t key) {
  if (!on_btns) {
    if (key == KEY_UP) port = 0;
    else if (key == KEY_DOWN && port == 1) on_btns = true;
    else if (key == KEY_DOWN) port = 1;
    else if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_TAB || key == KEY_ENTER) on_btns = true;
    else return;
    list_msg = NULL;
  } else {
    if (key == KEY_UP || key == KEY_TAB) on_btns = false;
    else if (key == KEY_LEFT || key == KEY_RIGHT) list_btn = can_forget() ? list_btn ^ 1 : 0;
    else if (key == KEY_ENTER && list_btn == 0) { list_msg = NULL; start(); draw(); return; }
    else if (key == KEY_ENTER) forget();
    else return;
  }
  if (!can_forget()) list_btn = 0;
  draw();
}

bool app_pad_mapper_listing() {
  return view == V_LIST;
}

void app_pad_mapper_on_keyboard() {
  // the joystick only on the ports: the mapping reads the pads itself
  uint8_t key = view == V_LIST ? ui_nav_key() : usb_keyboard_report.keycode[0];
  if (key == KEY_KPENTER) key = KEY_ENTER;
  if (key == KEY_ESC && view == V_LIST) {
    view = V_IDLE;
    osd_state = app_setup_back();
    return;
  }
  if (key == KEY_ESC || (key == KEY_ENTER && pl_state() >= PL_SAVED)) {
    show_list();
    return;
  }
  if (view == V_LIST) {
    list_on_keyboard(key);
    return;
  }
  if (key == KEY_ENTER && pl_state() == PL_PRESS) pl_skip();
  else if (key == KEY_ENTER && pl_state() == PL_DONE) pl_save();
  else if (key == KEY_BACKSPACE) pl_back();
  else return;
  draw();
}

void app_pad_mapper_tick() {
  if (view == V_LIST) {
    // the pads come and go
    if (ms_since(list_timer) >= LIST_REFRESH_MS) {
      list_timer = ms_now();
      usb_pad_t now[2];
      usb_pads_get(now);
      if (memcmp(now, pads, sizeof(now)) != 0) {
        memcpy(pads, now, sizeof(now));
        if (!can_forget()) list_btn = 0;
        draw();
      }
    }
  } else if (view == V_MAPPING && pl_tick()) {
    draw();
  }
}
