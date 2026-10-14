#pragma once

#include <Arduino.h>

extern volatile uint16_t joyL;
extern volatile uint16_t joyR;

void input_tick_buttons(bool btn1, bool btn2);
void input_tick_ui();        // APP: handle the events INPUT handed over (on_keyboard)
void input_tick_repeat();
void input_task_start(); // start the INPUT task (after the core is loaded)
void on_keyboard();
