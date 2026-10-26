#pragma once

#include <Arduino.h>

void matrix_begin();
void matrix_register_handlers();
void matrix_frame();     // one 20 ms frame, from the MATRIX task
bool matrix_present();
void matrix_progress(uint32_t val, uint32_t max);
void matrix_on_configure_start();
void matrix_on_configure_done(uint8_t core_type);

#define MATRIX_ICON_MSC 0
#define MATRIX_ICON_BOOTSEL 1
void matrix_icon(uint8_t icon); // a static icon instead of the scroll / meters (msc, bootsel)
