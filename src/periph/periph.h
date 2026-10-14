#pragma once

// Slow I2C peripherals (LED matrix, OLED). Plain build: periph_tick() from the main loop.
// FreeRTOS: a MATRIX task (above APP, exact 20 ms period) and an OLED task (APP's priority,
// time-sliced with it: a full OLED frame is ~12 ms of busy-polled I2C), created only for the
// devices that were found.

void periph_tick();        // plain build: call from the main loop (no-op under FreeRTOS)
void periph_task_start();  // FreeRTOS: start the MATRIX / OLED tasks (after matrix/oled are set up)
