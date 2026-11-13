#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_setup_overlay();
// the setup apps of this core (Wi-Fi and Settings only in the BOOT core): the menu of a v2 core
// shows them as its last items
uint8_t app_setup_count(void);
const char *app_setup_label(uint8_t pos);
uint8_t app_setup_state(uint8_t pos);
// where Esc of a setup app goes: the Setup screen, or the core menu when it was opened from there
void app_setup_opened_from_menu(bool from_menu);
uint8_t app_setup_back(void);
void app_setup_on_keyboard();

#ifdef __cplusplus
}
#endif
