#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_rtc_overlay();
void app_rtc_on_keyboard();
void app_rtc_tick();

#ifdef __cplusplus
}
#endif
