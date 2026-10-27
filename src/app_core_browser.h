#pragma once

#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "FT812.h"
#include "bitmaps.h"
#include "OSD.h"

extern core_list_item_t cores[MAX_CORES];
extern uint8_t cores_len;

void app_core_browser_overlay();
void app_core_browser_menu(uint8_t vpos);

void app_core_browser_ft_overlay();
void app_core_browser_ft_menu(uint8_t play_sounds);
// the parts of a graphic menu frame, shared with the graphic setup: the GRAM images and sounds
// (again after ft.init()), the background, then the clock, logo and picture on top
void app_core_browser_ft_load();
void app_core_browser_ft_background(const setup_t &t);
void app_core_browser_ft_decor(const setup_t &t, bool show_char);
// the start sound 1..4; wait: until it ends
void app_core_browser_ft_sound(uint8_t sound, bool wait);

core_list_item_t app_core_browser_get_item(const char *filename);
void app_core_browser_read_list();

void app_core_browser_on_keyboard();
void app_core_browser_tick(); // the mouse

void app_core_browser_on_time();
