#pragma once

#include <Arduino.h>

// OSD gamepad mapper (Setup > Map USB gamepad): learns the 12 MD2 buttons of a USB HID pad
// from its raw reports and writes /drivers/<vid>_<pid>.ini.
void app_pad_mapper_overlay();
void app_pad_mapper_on_keyboard();
void app_pad_mapper_tick();
