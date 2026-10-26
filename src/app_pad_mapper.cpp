#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "OSD.h"
#include "usb_hid_keys.h"
#include "hid_driver.h"
#include "hid_learn.h"
#include "app_pad_mapper.h"

// First the two joystick ports: the pad on each and what maps it (a driver ini, the default
// map, XInput); Forget renames the driver ini of a pad to .bak. Map: find the pad (the first
// one whose report changes), let it rest to learn the idle report and its jitter, then for
// each button wait for a press and for the release.
// The keyboard drives the screen: the pad being mapped must not.

enum {
  ST_IDLE,    // not on screen
  ST_LIST,    // the ports
  ST_SELECT,  // waiting for a pad
  ST_SETTLE,  // a pad was found, give it time to be released
  ST_REST,    // collecting the idle reports
  ST_PRESS,   // waiting for the press of button `btn`
  ST_RELEASE, // waiting for the release
  ST_DONE,    // all buttons done, save?
  ST_SAVED,
  ST_FAILED,
};

#define SETTLE_MS 700
#define REST_MS 700
#define PRESS_TIMEOUT_MS 10000
#define RELEASE_TIMEOUT_MS 3000
#define LIST_REFRESH_MS 500
#define BTN_WIDTH 10

static const char *const btn_names[HID_JOY_BUTTONS] = {"Up", "Down", "Left", "Right", "A", "B", "C", "X", "Y", "Z", "Start", "Mode"};

static uint8_t state = ST_IDLE;
static uint8_t btn;
static ElapsedTimer timer;
static uint32_t last_seq;
static hid_capture_t rep;
static hid_learn_rest_t rest;
static hid_joy_rule_t rules[HID_JOY_BUTTONS];
static bool mapped[HID_JOY_BUTTONS];
static char saved_path[32];
static usb_pad_t pads[2];
static uint8_t port;       // the port chosen in the list
static uint8_t list_btn;   // 0: Map, 1: Forget
static const char *list_msg;
static bool list_msg_ok;

static void set_state(uint8_t s) {
  state = s;
  timer.reset();
}

static void draw_status(const char *line1, const char *line2 = "") {
  zxosd.setColor(OSD::COLOR_YELLOW_I, OSD::COLOR_BLACK);
  zxosd.fill(0, 8, 31, 9, ' ');
  zxosd.setPos(0, 8); zxosd.print(line1);
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.setPos(0, 9); zxosd.print(line2);
}

static void draw_buttons() {
  for (uint8_t i = 0; i < HID_JOY_BUTTONS; i++) {
    bool current = (state == ST_PRESS || state == ST_RELEASE) && i == btn;
    zxosd.setColor(current ? OSD::COLOR_BLACK : OSD::COLOR_WHITE, current ? OSD::COLOR_WHITE : OSD::COLOR_BLACK);
    zxosd.fill(0, 11 + i, 31, 11 + i, ' ');
    zxosd.setPos(1, 11 + i); zxosd.print(btn_names[i]);
    zxosd.setPos(8, 11 + i);
    if (mapped[i]) {
      char buf[24];
      hid_learn_format_rule(&rules[i], buf, sizeof(buf));
      zxosd.print(buf);
    } else if (i < btn || state >= ST_DONE) {
      zxosd.print("-");
    }
  }
}

static void draw_footer(const char *line1, const char *line2) {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.fill(0, 24, 31, 25, ' ');
  zxosd.setPos(1, 24); zxosd.print(line1);
  zxosd.setPos(1, 25); zxosd.print(line2);
}

static bool can_forget() { return pads[port].source == USB_PAD_DRIVER; }

// the source of a pad, at most 17 chars
static void pad_source(const usb_pad_t &p, char *out, size_t size) {
  switch (p.source) {
    case USB_PAD_DRIVER: snprintf(out, size, "%.17s", p.file); break;
    case USB_PAD_DEFAULT: strlcpy(out, "default map", size); break;
    case USB_PAD_XINPUT: strlcpy(out, "XInput", size); break;
    default: strlcpy(out, "no map", size);
  }
}

static void draw_list() {
  zxosd.setColor(OSD::COLOR_GREY, OSD::COLOR_BLACK);
  zxosd.fill(0, 8, 31, 22, ' ');
  zxosd.setPos(1, 8); zxosd.print("Port Pad       Mapped by");
  for (uint8_t i = 0; i < 2; i++) {
    const usb_pad_t &p = pads[i];
    bool chosen = i == port;
    zxosd.setColor(chosen ? OSD::COLOR_BLACK : OSD::COLOR_WHITE, chosen ? OSD::COLOR_WHITE : OSD::COLOR_BLACK);
    zxosd.fill(0, 10 + i, 31, 10 + i, ' ');
    zxosd.setPos(1, 10 + i);
    char src[18], buf[33];
    if (p.source == USB_PAD_NONE && p.vid == 0) {
      snprintf(buf, sizeof(buf), "%u    -", i + 1);
    } else {
      pad_source(p, src, sizeof(src));
      snprintf(buf, sizeof(buf), "%u    %04x:%04x %s", i + 1, p.vid, p.pid, src);
    }
    zxosd.print(buf);
  }
  ui_button(1, 14, BTN_WIDTH, "Map", list_btn == 0);
  if (can_forget()) ui_button(12, 14, BTN_WIDTH, "Forget", list_btn == 1);
  if (list_msg) {
    zxosd.setColor(list_msg_ok ? OSD::COLOR_GREEN_I : OSD::COLOR_RED_I, OSD::COLOR_BLACK);
    zxosd.setPos(1, 16); zxosd.print(list_msg);
  }
  zxosd.setColor(OSD::COLOR_GREY, OSD::COLOR_BLACK);
  zxosd.setPos(1, 18); zxosd.print("Map: learns the pad you press a");
  zxosd.setPos(1, 19); zxosd.print("button on, also one not on a");
  zxosd.setPos(1, 20); zxosd.print("port");
  zxosd.setPos(1, 21); zxosd.print("Forget: the driver ini -> .bak,");
  zxosd.setPos(1, 22); zxosd.print("the pad uses its default map");
  draw_footer("Up/Down: port, Left/Right: btn", "Enter: OK, Esc: return");
}

static void draw() {
  char buf[33];
  if (state == ST_LIST) {
    draw_list();
    zxosd.update();
    return;
  }
  switch (state) {
    case ST_SELECT:
      draw_status("Press a button on the USB pad");
      break;
    case ST_SETTLE:
    case ST_REST:
      snprintf(buf, sizeof(buf), "Pad %04x:%04x", rep.vid, rep.pid);
      draw_status(buf, "Release all, don't touch it");
      break;
    case ST_PRESS:
      snprintf(buf, sizeof(buf), "Press %s", btn_names[btn]);
      draw_status(buf, "Enter: skip, Backspace: back");
      break;
    case ST_RELEASE:
      snprintf(buf, sizeof(buf), "Release %s", btn_names[btn]);
      draw_status(buf);
      break;
    case ST_DONE:
      draw_status("Done. Enter: save the driver", "Backspace: redo the last one");
      break;
    case ST_SAVED:
      draw_status("Saved", saved_path);
      break;
    case ST_FAILED:
      draw_status("Could not write the SD card");
      break;
  }
  draw_buttons();
  draw_footer(state >= ST_SAVED ? "Enter: back to the ports" : "", "Esc: back to the ports");
  zxosd.update();
}

static void show_list() {
  hid_capture_stop();
  usb_pads_get(pads);
  list_btn = 0;
  set_state(ST_LIST);
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.fill(0, 7, 31, 22, ' ');
  draw();
}

static void start() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.fill(0, 7, 31, 22, ' ');
  btn = 0;
  last_seq = 0;
  memset(mapped, 0, sizeof(mapped));
  hid_learn_rest_reset(&rest);
  hid_capture_start();
  set_state(ST_SELECT);
}

void app_pad_mapper_overlay() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();
  zxosd.header(core.build, core.id, HW_ID);
  print_time();
  zxosd.setPos(0, 5);
  zxosd.print("Map USB gamepad");
  zxosd.line(6);

  zxosd.line(23);
  port = 0;
  list_msg = nullptr;
  show_list();
}

static void next_button() {
  btn++;
  set_state(btn < HID_JOY_BUTTONS ? ST_PRESS : ST_DONE);
  draw();
}

static void prev_button() {
  if (btn == 0) return;
  btn--;
  mapped[btn] = false;
  set_state(ST_PRESS);
  draw();
}

static void save() {
  hid_joy_config_t cfg;
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
  hid_capture_stop();
  set_state(hid_driver_save(&cfg, saved_path, sizeof(saved_path)) ? ST_SAVED : ST_FAILED);
  draw();
}

static void forget() {
  bool ok = hid_driver_forget(pads[port].vid, pads[port].pid);
  list_msg = ok ? "The driver is forgotten" : "Could not rename the driver";
  list_msg_ok = ok;
  usb_pads_get(pads);
  list_btn = 0;
}

static void list_on_keyboard(uint8_t key) {
  if (key == KEY_UP || key == KEY_DOWN) { port ^= 1; list_msg = nullptr; }
  else if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_TAB) list_btn = can_forget() ? list_btn ^ 1 : 0;
  else if ((key == KEY_ENTER || key == KEY_KPENTER) && list_btn == 0) { list_msg = nullptr; start(); draw(); return; }
  else if (key == KEY_ENTER || key == KEY_KPENTER) forget();
  else return;
  if (!can_forget()) list_btn = 0;
  draw();
}

void app_pad_mapper_on_keyboard() {
  uint8_t key = usb_keyboard_report.keycode[0];
  if (key == KEY_ESC && state == ST_LIST) {
    state = ST_IDLE;
    osd_state = state_setup;
    return;
  }
  if (key == KEY_ESC || (key == KEY_ENTER && state >= ST_SAVED)) {
    show_list();
    return;
  }
  if (state == ST_LIST) {
    list_on_keyboard(key);
    return;
  }
  if (key == KEY_ENTER && state == ST_PRESS) {
    next_button(); // skip
  } else if (key == KEY_ENTER && state == ST_DONE) {
    bool any = false;
    for (uint8_t i = 0; i < HID_JOY_BUTTONS; i++) any |= mapped[i];
    if (any) save();
  } else if (key == KEY_BACKSPACE && (state == ST_PRESS || state == ST_RELEASE || state == ST_DONE)) {
    prev_button();
  }
}

void app_pad_mapper_tick() {
  bool fresh = hid_capture_get(&rep) && rep.seq != last_seq;
  if (fresh) last_seq = rep.seq;

  switch (state) {
    case ST_LIST:
      // the pads come and go
      if (timer.elapsed() >= LIST_REFRESH_MS) {
        timer.reset();
        usb_pad_t now[2];
        usb_pads_get(now);
        if (memcmp(now, pads, sizeof(now)) != 0) {
          memcpy(pads, now, sizeof(now));
          if (!can_forget()) list_btn = 0;
          draw();
        }
      }
      break;
    case ST_SELECT:
      if (fresh) { set_state(ST_SETTLE); draw(); }
      break;
    case ST_SETTLE:
      if (timer.elapsed() >= SETTLE_MS) { hid_learn_rest_reset(&rest); set_state(ST_REST); }
      break;
    case ST_REST:
      if (fresh) hid_learn_rest_add(&rest, rep.report_id, rep.data, rep.len);
      // a pad may only report on changes: the last report is the idle one then
      if (timer.elapsed() >= REST_MS) {
        if (rest.len == 0) hid_learn_rest_add(&rest, rep.report_id, rep.data, rep.len);
        btn = 0;
        set_state(ST_PRESS);
        draw();
      }
      break;
    case ST_PRESS:
      if (fresh && hid_learn_detect(&rest, rep.report_id, rep.data, rep.len, &rules[btn])) {
        mapped[btn] = true;
        set_state(ST_RELEASE);
        draw();
      } else if (timer.elapsed() >= PRESS_TIMEOUT_MS) {
        next_button(); // skip
      }
      break;
    case ST_RELEASE:
      if ((fresh && hid_learn_is_rest(&rest, rep.report_id, rep.data, rep.len)) || timer.elapsed() >= RELEASE_TIMEOUT_MS) {
        next_button();
      }
      break;
  }
}
