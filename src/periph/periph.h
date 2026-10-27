#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Slow I2C peripherals (LED matrix, OLED): a MATRIX task (above APP, exact 20 ms period) and an
// OLED task (APP's priority, time-sliced with it: a full OLED frame is ~12 ms of busy-polled
// I2C), created only for the devices that were found.

void periph_task_start();  // start the MATRIX / OLED tasks (after matrix/oled are set up)

#ifdef __cplusplus
}
#endif
