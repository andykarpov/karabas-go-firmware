#pragma once

// Slow I2C peripherals (LED matrix, OLED). Plain build: periph_tick() from the main loop.
// FreeRTOS: a PERIPH task at APP's priority runs them, so the scheduler time-slices the long
// busy-polled I2C transfers (a full OLED frame is ~46 ms at 100 kHz) with the APP loop instead
// of stalling it.

void periph_tick();        // plain build: call from the main loop (no-op under FreeRTOS)
void periph_task_start();  // FreeRTOS: start the PERIPH task (after matrix/oled are set up)
