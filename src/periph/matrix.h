#pragma once

#include <Arduino.h>

void matrix_begin();
void matrix_register_handlers();
void matrix_tick();      // plain build: from the main loop, runs matrix_frame() every 20 ms
void matrix_frame();     // one 20 ms frame (FreeRTOS: from the MATRIX task)
bool matrix_present();
void matrix_progress(uint32_t val, uint32_t max);
void matrix_on_configure_start();
void matrix_on_configure_done(uint8_t core_type);
