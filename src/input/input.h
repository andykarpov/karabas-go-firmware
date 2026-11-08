#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint16_t joyL;
extern volatile uint16_t joyR;

void input_tick_buttons();  // the on-board buttons by the actions of the core (core.btn)
void input_tick_ui();        // APP: handle the events INPUT handed over (on_keyboard)
void input_tick_repeat();
void input_task_start(); // start the INPUT task (after the core is loaded)
bool input_is_menu_hotkey(const hid_keyboard_report_t *kbd); // the report is a Menu+key hotkey of the core
void on_keyboard();
void flush_and_reboot(); // the backup RAM and the core EEPROM saved, then back to the boot core

#ifdef __cplusplus
}
#endif
