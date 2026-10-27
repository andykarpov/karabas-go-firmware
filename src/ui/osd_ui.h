#pragma once

#include <Arduino.h>
#include "OSD.h"
#include "ms_time.h"
#include "Adafruit_TinyUSB.h"
#include "SegaController.h"

// the joystick Esc of the menus: C, or Select (Mode) on pads without C
#define UI_JOY_ESC (SC_BTN_C | SC_BTN_MODE)

extern OSD zxosd;
extern volatile bool is_osd;
extern volatile bool is_osd_hiding;
extern bool need_redraw;
extern bool has_ft;
extern uint32_t hide_timer;
extern uint8_t osd_state;
extern uint8_t osd_return_state;
// on_keyboard() runs for a held key or joystick direction, not a press (Left as back ignores it)
extern bool ui_repeat;

void osd_handle(bool force);
void ui_on_keyboard();
void ui_popup_start(uint8_t i);
void ui_tick();
void print_time();
// a button of the OSD apps: width chars, the label centered; yellow when focused, else grey
void ui_button(uint8_t x, uint8_t y, uint8_t width, const char *label, bool focused);
void on_time();
// the keys pressed since prev (up to 6, in order), or the repeated key when the report did not
// change; prev becomes the current report. For screens that read every key (text fields).
uint8_t ui_pressed_keys(hid_keyboard_report_t *prev, uint8_t *keys);
// the joystick (both ports) as a key: KEY_UP / DOWN / LEFT / RIGHT, KEY_ENTER (A, B), KEY_ESC (C, Select); 0 if none
uint8_t ui_joy_key();
// a joystick key in a free text field: Up / Down turn the last char, Right adds one, Left
// deletes it. false if the key is not one of these.
bool ui_joy_edit(char *text, size_t max, uint8_t key);
// false while a screen reads the joystick itself (the pad mapping): no repeat of the directions
bool ui_joy_repeats();
// false while a screen reads the joystick itself: no global joystick hotkeys (Start+X reboots)
bool ui_joy_hotkeys();
