#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// OSD gamepad mapper (Setup > Map USB gamepad): learns the 12 MD2 buttons of a USB HID pad
// from its raw reports and writes /drivers/<vid>_<pid>.ini.
void app_pad_mapper_overlay();
void app_pad_mapper_on_keyboard();
void app_pad_mapper_tick();
// true on the list of the ports (not mapping a pad)
bool app_pad_mapper_listing();

#ifdef __cplusplus
}
#endif
