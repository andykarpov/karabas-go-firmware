#include "config.h"
#include "types.h"
#include "main.h"
#include "osd.h"
#include "input/usb_hid_keys.h"
#include "usb/hid_driver.h"
#include "hid_learn.h"
#include "apps/pad_learn.h"
#include "apps/app_pad_mapper.h"
#include "ui/ui_draw.h"

// First the two joystick ports: the pad on each and what maps it (a driver ini, the default
// map, XInput); Forget renames the driver ini of a pad to .bak. Map learns a pad through
// apps/pad_learn (shared with the Gamepad page of the graphic setup); this screen draws it.
// The keyboard drives the screen: the pad being mapped must not.

enum {
  V_IDLE,    // not on screen
  V_LIST,    // the ports
  V_MAPPING, // pad_learn runs
};

#define LIST_REFRESH_MS 500
#define BTN_WIDTH 10

static uint8_t view = V_IDLE;
static uint32_t list_timer = 0;
static usb_pad_t pads[2];
static uint8_t port;       // the port chosen in the list
static uint8_t list_btn;   // 0: Map, 1: Forget
static const char *list_msg;
static bool list_msg_ok;

static void draw_status(const char *line1, const char *line2) {
  osd_set_color(OSD_COLOR_YELLOW_I, OSD_COLOR_BLACK);
  osd_fill(0, 8, 31, 9, ' ');
  osd_set_pos(0, 8); osd_print(line1);
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_set_pos(0, 9); osd_print(line2);
}

static void draw_buttons() {
  uint8_t st = pl_state();
  for (uint8_t i = 0; i < HID_JOY_BUTTONS; i++) {
    bool current = (st == PL_PRESS || st == PL_RELEASE) && i == pl_btn();
    osd_set_color(current ? OSD_COLOR_BLACK : OSD_COLOR_WHITE, current ? OSD_COLOR_WHITE : OSD_COLOR_BLACK);
    osd_fill(0, 11 + i, 31, 11 + i, ' ');
    osd_set_pos(1, 11 + i); osd_print(pl_btn_names[i]);
    osd_set_pos(8, 11 + i);
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
  osd_set_color(OSD_COLOR_GREY, OSD_COLOR_BLACK);
  osd_fill(0, 8, 31, 22, ' ');
  osd_set_pos(1, 8); osd_print("Port Pad       Mapped by");
  for (uint8_t i = 0; i < 2; i++) {
    const usb_pad_t *p = &pads[i];
    bool chosen = i == port;
    osd_set_color(chosen ? OSD_COLOR_BLACK : OSD_COLOR_WHITE, chosen ? OSD_COLOR_WHITE : OSD_COLOR_BLACK);
    osd_fill(0, 10 + i, 31, 10 + i, ' ');
    osd_set_pos(1, 10 + i);
    char src[18], buf[33];
    if (p->source == USB_PAD_NONE && p->vid == 0) {
      snprintf(buf, sizeof(buf), "%u    -", i + 1);
    } else {
      pad_source(p, src, sizeof(src));
      snprintf(buf, sizeof(buf), "%u    %04x:%04x %s", i + 1, p->vid, p->pid, src);
    }
    osd_print(buf);
  }
  ui_button(1, 14, BTN_WIDTH, "Map", list_btn == 0);
  if (can_forget()) ui_button(12, 14, BTN_WIDTH, "Forget", list_btn == 1);
  if (list_msg) {
    osd_set_color(list_msg_ok ? OSD_COLOR_GREEN_I : OSD_COLOR_RED_I, OSD_COLOR_BLACK);
    osd_set_pos(1, 16); osd_print(list_msg);
  }
  osd_set_color(OSD_COLOR_GREY, OSD_COLOR_BLACK);
  osd_set_pos(1, 18); osd_print("Map: learns the pad you press a");
  osd_set_pos(1, 19); osd_print("button on, also one not on a");
  osd_set_pos(1, 20); osd_print("port");
  osd_set_pos(1, 21); osd_print("Forget: the driver ini -> .bak,");
  osd_set_pos(1, 22); osd_print("the pad uses its default map");
  ui_hint("Up/Down: port, Left/Right: btn", "Enter: OK, Esc: return");
}

static void draw() {
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
  view = V_LIST;
  list_timer = ms_now();
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_fill(0, 7, 31, 22, ' ');
  draw();
}

static void start() {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_fill(0, 7, 31, 22, ' ');
  view = V_MAPPING;
  pl_start();
}

void app_pad_mapper_overlay() {
  ui_screen("Map USB gamepad");
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

static void list_on_keyboard(uint8_t key) {
  if (key == KEY_UP || key == KEY_DOWN) { port ^= 1; list_msg = NULL; }
  else if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_TAB) list_btn = can_forget() ? list_btn ^ 1 : 0;
  else if (key == KEY_ENTER && list_btn == 0) { list_msg = NULL; start(); draw(); return; }
  else if (key == KEY_ENTER) forget();
  else return;
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
    osd_state = state_setup;
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
