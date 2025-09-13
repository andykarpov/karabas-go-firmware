#pragma once

#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "FT812.h"
#include "bitmaps.h"
#include "OSD.h"

void app_core_browser_overlay();
void app_core_browser_menu(uint8_t vpos);

void app_core_browser_ft_overlay();
void app_core_browser_ft_menu(uint8_t play_sounds);

core_list_item_t app_core_browser_get_item();
void app_core_browser_read_list();

void app_core_browser_on_keyboard();

void app_core_browser_on_time();
