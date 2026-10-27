#include "i2c_bus.h"
#include "diag/hang_diag.h"

#include <FreeRTOS.h>
#include <semphr.h>
static SemaphoreHandle_t i2c_mutex = NULL;

void i2c_bus_init() {
  i2c_mutex = xSemaphoreCreateRecursiveMutex();
}

void i2c_bus_lock(void) {
  if (!i2c_mutex) return;
  hang_lock_wait(HANG_LOCK_I2C);
  xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);
  hang_lock_held(HANG_LOCK_I2C);
}

void i2c_bus_unlock(void) {
  if (!i2c_mutex) return;
  xSemaphoreGiveRecursive(i2c_mutex);
  hang_lock_free(HANG_LOCK_I2C, xSemaphoreGetMutexHolder(i2c_mutex) != NULL);
}
