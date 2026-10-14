#pragma once

#include <Arduino.h>

extern uint16_t joyL;
extern uint16_t joyR;

void input_tick_buttons(bool btn1, bool btn2);
void input_tick_joysticks();
void input_tick_repeat();
void input_drain_events();
void on_keyboard();
