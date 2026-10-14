#include "i2c_bus.h"

#ifdef __FREERTOS
#include <FreeRTOS.h>
#include <semphr.h>
static SemaphoreHandle_t i2c_mutex = nullptr;
#endif

void i2c_bus_init() {
#ifdef __FREERTOS
  i2c_mutex = xSemaphoreCreateRecursiveMutex();
#endif
}

extern "C" void i2c_bus_lock(void) {
#ifdef __FREERTOS
  if (i2c_mutex) xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);
#endif
}

extern "C" void i2c_bus_unlock(void) {
#ifdef __FREERTOS
  if (i2c_mutex) xSemaphoreGiveRecursive(i2c_mutex);
#endif
}
