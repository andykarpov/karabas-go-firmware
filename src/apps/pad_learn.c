#include "main.h"
#include "apps/pad_learn.h"

#define SETTLE_MS 700
#define REST_MS 700
#define REST_GIVE_UP_MS 3000 // a pad that never rests (a counter byte): take what there is
#define PRESS_TIMEOUT_MS 10000
#define RELEASE_TIMEOUT_MS 3000

const char *const pl_btn_names[HID_JOY_BUTTONS] = {"Up", "Down", "Left", "Right", "A", "B", "C", "X", "Y", "Z", "Start", "Mode"};

static uint8_t state = PL_IDLE;
static uint8_t btn;
static int8_t last_btn = -1;
static uint32_t timer = 0;
static uint32_t rest_start = 0;
static uint32_t last_seq;
static hid_capture_t rep;
static hid_learn_rest_t rest;
static hid_joy_rule_t rules[HID_JOY_BUTTONS];
static bool mapped[HID_JOY_BUTTONS];
static hid_joy_config_t cfg; // the learned map (PL_DONE and later)
static uint16_t live;
static char saved_path[32];
static bool changed;

static void set_state(uint8_t s) {
  state = s;
  timer = ms_now();
  changed = true;
}

static void build_cfg(void) {
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

void pl_start(void) {
  btn = 0;
  last_btn = -1;
  last_seq = 0;
  live = 0;
  memset(mapped, 0, sizeof(mapped));
  hid_learn_rest_reset(&rest);
  hid_capture_start();
  set_state(PL_SELECT);
}

void pl_stop(void) {
  hid_capture_stop();
  state = PL_IDLE;
}

uint8_t pl_state(void) { return state; }
uint8_t pl_btn(void) { return btn; }
int8_t pl_last_btn(void) { return last_btn; }
bool pl_mapped(uint8_t i) { return mapped[i]; }
const hid_joy_rule_t *pl_rule(uint8_t i) { return &rules[i]; }
uint16_t pl_vid(void) { return rep.vid; }
uint16_t pl_pid(void) { return rep.pid; }
uint16_t pl_live(void) { return live; }
const char *pl_saved_path(void) { return saved_path; }

uint32_t pl_press_left_s(void) {
  uint32_t t = ms_since(timer);
  return ((t < PRESS_TIMEOUT_MS ? PRESS_TIMEOUT_MS - t : 0) + 999) / 1000;
}

static void next_button(void) {
  last_btn = btn;
  btn++;
  if (btn < HID_JOY_BUTTONS) {
    set_state(PL_PRESS);
  } else {
    build_cfg();
    set_state(PL_DONE);
  }
}

void pl_skip(void) {
  if (state == PL_PRESS) next_button();
}

void pl_back(void) {
  if ((state != PL_PRESS && state != PL_RELEASE && state != PL_DONE) || btn == 0) return;
  btn--;
  mapped[btn] = false;
  last_btn = btn > 0 ? btn - 1 : -1;
  set_state(PL_PRESS);
}

void pl_save(void) {
  if (state != PL_DONE) return;
  bool any = false;
  for (uint8_t i = 0; i < HID_JOY_BUTTONS; i++) any |= mapped[i];
  if (!any) return;
  build_cfg();
  // the capture goes on: a screen may keep showing the presses through the new map
  set_state(hid_driver_save(&cfg, saved_path, sizeof(saved_path)) ? PL_SAVED : PL_FAILED);
}

bool pl_tick(void) {
  if (state == PL_IDLE) return false;
  bool fresh = hid_capture_get(&rep) && rep.seq != last_seq;
  if (fresh) last_seq = rep.seq;

  switch (state) {
    case PL_SELECT:
      if (fresh) set_state(PL_SETTLE);
      break;
    case PL_SETTLE:
      if (ms_since(timer) >= SETTLE_MS) {
        hid_learn_rest_reset(&rest);
        rest_start = ms_now();
        state = PL_REST;
        timer = ms_now();
      }
      break;
    case PL_REST:
      // touched while resting (a direction pressed again): it would become the rest, and its
      // press would be learned as the opposite one. Start the rest again
      if (fresh && hid_learn_rest_moved(&rest, rep.report_id, rep.data, rep.len) &&
          ms_since(rest_start) < REST_GIVE_UP_MS) {
        hid_learn_rest_reset(&rest);
        timer = ms_now();
        break;
      }
      if (fresh) hid_learn_rest_add(&rest, rep.report_id, rep.data, rep.len);
      // a pad may only report on changes: the last report is the idle one then
      if (ms_since(timer) >= REST_MS) {
        if (rest.len == 0) hid_learn_rest_add(&rest, rep.report_id, rep.data, rep.len);
        btn = 0;
        set_state(PL_PRESS);
      }
      break;
    case PL_PRESS:
      if (fresh && hid_learn_detect(&rest, rep.report_id, rep.data, rep.len, &rules[btn])) {
        mapped[btn] = true;
        last_btn = btn;
        set_state(PL_RELEASE);
      } else if (ms_since(timer) >= PRESS_TIMEOUT_MS) {
        next_button(); // skip
      }
      break;
    case PL_RELEASE:
      if ((fresh && hid_learn_is_rest(&rest, rep.report_id, rep.data, rep.len)) || ms_since(timer) >= RELEASE_TIMEOUT_MS) {
        next_button();
      }
      break;
    default: // done or saved: the presses through the learned map
      if (fresh && hid_joy_accepts_report(&cfg, rep.report_id)) {
        uint16_t held = hid_joy_map_report(&cfg, rep.data, rep.len, 0);
        if (held != live) { live = held; changed = true; }
      }
  }
  bool c = changed;
  changed = false;
  return c;
}
