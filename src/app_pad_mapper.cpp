#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "OSD.h"
#include "usb_hid_keys.h"
#include "hid_driver.h"
#include "hid_learn.h"
#include "app_pad_mapper.h"

// Steps: find the pad (the first one whose report changes), let it rest to learn the idle
// report and its jitter, then for each button wait for a press and for the release.
// The keyboard drives the screen: the pad being mapped must not.

enum {
  ST_IDLE,    // not on screen
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

static void draw() {
  char buf[33];
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
  zxosd.update();
}

static void start() {
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

  // footer
  zxosd.line(24);
  zxosd.setPos(1, 25); zxosd.print("Press Esc to return");
  start();
  draw();
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

void app_pad_mapper_on_keyboard() {
  uint8_t key = usb_keyboard_report.keycode[0];
  if (key == KEY_ESC) {
    hid_capture_stop();
    state = ST_IDLE;
    osd_state = state_setup;
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
