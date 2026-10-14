#include "main.h"
#include "periph.h"

#ifndef __FREERTOS

void periph_tick() {
  matrix_tick();
  oled_tick();
}

void periph_task_start() {}

#else

#include <FreeRTOS.h>
#include <task.h>

#define PERIPH_TASK_STACK 1536 // words
#define PERIPH_TASK_PRIORITY (configMAX_PRIORITIES / 2) // same as APP: time-sliced with it

static void periph_task(void *param) {
  for (;;) {
    matrix_tick();
    oled_tick();
    // the ticks run on their own 20 / 100 ms timers
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void periph_tick() {}

void periph_task_start() {
  if (xTaskCreateAffinitySet(periph_task, "PERIPH", PERIPH_TASK_STACK, nullptr, PERIPH_TASK_PRIORITY, 1 << 0, nullptr) != pdPASS) {
    panic("Unable to create PERIPH task");
  }
}

#endif
