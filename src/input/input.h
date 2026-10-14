#pragma once

#include <Arduino.h>

extern volatile uint16_t joyL;
extern volatile uint16_t joyR;

void input_tick_buttons(bool btn1, bool btn2);
void input_tick_joysticks();
void input_tick_repeat();
void input_drain_events();
void input_task_start(); // FreeRTOS: start the INPUT task (after the core is loaded)
void on_keyboard();
