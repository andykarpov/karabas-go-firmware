#pragma once

#include <Arduino.h>

void app_ft_setup_overlay();
void app_ft_setup_on_keyboard();
void app_ft_setup_tick();
// redraws the graphic setup (the clock every second)
void app_ft_setup_draw();
// the Clock page saved the time zone: the edited copy follows, Save does not write the old one
void app_ft_setup_tz_saved(int16_t tz);
