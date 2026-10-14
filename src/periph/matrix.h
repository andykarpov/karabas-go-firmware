#pragma once

#include <Arduino.h>

void matrix_begin();
void matrix_register_handlers();
void matrix_tick();
void matrix_progress(uint32_t val, uint32_t max);
void matrix_on_configure_start();
void matrix_on_configure_done(uint8_t core_type);
