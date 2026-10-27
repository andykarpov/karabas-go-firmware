#include "main.h"
#include "periph.h"

#include <FreeRTOS.h>
#include <task.h>

// MATRIX: above APP with an exact period, so the animation stays smooth. A frame is two short
// HT16K33 writes (well under 1 ms at 400 kHz).
// OLED: a full frame is ~12 ms of busy-polled I2C, so it runs at APP's priority (time-sliced
// with APP) instead of taking the CPU away from it. While it holds the I2C bus a matrix frame
// may wait for it (priority inheritance lets it finish first).
#define MATRIX_TASK_STACK 1024 // words
#define MATRIX_TASK_PRIORITY (configMAX_PRIORITIES / 2 + 1)
#define OLED_TASK_STACK 1024 // words
#define OLED_TASK_PRIORITY (configMAX_PRIORITIES / 2)
#define FRAME_MS 20

static void matrix_task(void *param) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(FRAME_MS));
    matrix_frame();
  }
}

static void oled_task(void *param) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(FRAME_MS));
    oled_frame();
  }
}

void periph_task_start() {
  if (matrix_present() &&
      xTaskCreateAffinitySet(matrix_task, "MATRIX", MATRIX_TASK_STACK, NULL, MATRIX_TASK_PRIORITY, 1 << 0, NULL) != pdPASS) {
    panic("Unable to create MATRIX task");
  }
  if (oled_present() &&
      xTaskCreateAffinitySet(oled_task, "OLED", OLED_TASK_STACK, NULL, OLED_TASK_PRIORITY, 1 << 0, NULL) != pdPASS) {
    panic("Unable to create OLED task");
  }
}

