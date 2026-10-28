#pragma once

// The learning of a USB pad shared by Setup > Map USB gamepad (app_pad_mapper, text) and the
// Gamepad page of the graphic setup (app_ft_pad): find the pad (the first one whose report
// changes), let it rest to learn the idle report and its jitter, then for each MD2 button wait
// for a press and for the release; save a driver ini. The screens draw the steps and keep their
// own list of the ports.

#include <stdbool.h>
#include <stdint.h>
#include "usb/hid_driver.h"
#include "hid_learn.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  PL_IDLE,    // not learning
  PL_SELECT,  // waiting for a pad
  PL_SETTLE,  // a pad was found, give it time to be released
  PL_REST,    // collecting the idle reports
  PL_PRESS,   // waiting for the press of button pl_btn()
  PL_RELEASE, // waiting for the release
  PL_DONE,    // all buttons done, save?
  PL_SAVED,
  PL_FAILED,
};

extern const char *const pl_btn_names[HID_JOY_BUTTONS]; // "Up" ... "Mode"

// starts learning (the capture of the reports on)
void pl_start(void);
// stops learning and the capture
void pl_stop(void);
// the reports of the pad: the steps go on; true if what a screen shows changed
bool pl_tick(void);

uint8_t pl_state(void);
uint8_t pl_btn(void);                // the button asked for
int8_t pl_last_btn(void);            // the button learned last, -1 if none
bool pl_mapped(uint8_t i);
const hid_joy_rule_t *pl_rule(uint8_t i);
uint16_t pl_vid(void);
uint16_t pl_pid(void);
// the buttons held through the learned map (PL_DONE and later)
uint16_t pl_live(void);
const char *pl_saved_path(void);
// PL_PRESS: the seconds until the button is skipped
uint32_t pl_press_left_s(void);

void pl_skip(void);                  // PL_PRESS: the button stays unmapped
void pl_back(void);                  // the button before again
// PL_DONE: writes the driver ini (PL_SAVED / PL_FAILED); nothing if no button was learned
void pl_save(void);

#ifdef __cplusplus
}
#endif
