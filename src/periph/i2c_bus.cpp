#include "i2c_bus.h"

#include <FreeRTOS.h>
#include <semphr.h>
static SemaphoreHandle_t i2c_mutex = nullptr;

void i2c_bus_init() {
  i2c_mutex = xSemaphoreCreateRecursiveMutex();
}

extern "C" void i2c_bus_lock(void) {
  if (i2c_mutex) xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);
}

extern "C" void i2c_bus_unlock(void) {
  if (i2c_mutex) xSemaphoreGiveRecursive(i2c_mutex);
}
