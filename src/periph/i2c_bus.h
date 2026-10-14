#pragma once

// I2C bus lock (recursive), no-op without FreeRTOS. The bus is shared by the PERIPH task
// (LED matrix, OLED) and APP (RTC + EEPROM, matrix commands from the fpga, Go io extender).
// Lock order: the SPI bus lock may be held when taking this one, never the other way round:
// release the I2C lock before calling spi_send().

void i2c_bus_init();                    // FreeRTOS: create the lock (before the tasks start)
extern "C" void i2c_bus_lock(void);     // also used by lib/RTC
extern "C" void i2c_bus_unlock(void);

struct I2cGuard {
  I2cGuard() { i2c_bus_lock(); }
  ~I2cGuard() { i2c_bus_unlock(); }
  I2cGuard(const I2cGuard&) = delete;
  I2cGuard& operator=(const I2cGuard&) = delete;
};
