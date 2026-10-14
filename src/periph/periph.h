#pragma once

// Slow I2C peripherals (LED matrix, OLED). Plain build: periph_tick() from the main loop.
// FreeRTOS: a PERIPH task at APP's priority runs them, so the scheduler time-slices the
// busy-polled I2C transfers (a full 512-byte OLED frame is ~12 ms at 400 kHz) with the APP loop
// instead of stalling it.

void periph_tick();        // plain build: call from the main loop (no-op under FreeRTOS)
void periph_task_start();  // FreeRTOS: start the PERIPH task (after matrix/oled are set up)
