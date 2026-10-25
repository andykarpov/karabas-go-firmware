#pragma once

#include <Arduino.h>
#include "OSD.h"
#include "ElapsedTimer.h"
#include "Adafruit_TinyUSB.h"

extern OSD zxosd;
extern volatile bool is_osd;
extern volatile bool is_osd_hiding;
extern bool need_redraw;
extern bool has_ft;
extern ElapsedTimer hide_timer;
extern uint8_t osd_state;
extern uint8_t osd_return_state;

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
