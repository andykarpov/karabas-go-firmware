#pragma once

#include <Arduino.h>

extern uint8_t curr_osd_item;

void app_core_overlay();
void app_core_menu(uint8_t vpos);
void app_core_save(uint8_t pos);
void app_core_on_keyboard();
void app_core_filebrowser(uint8_t vpos);
