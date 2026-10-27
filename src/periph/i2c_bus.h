#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// I2C bus lock (recursive). The bus is shared by the MATRIX and OLED tasks
// (LED matrix, OLED) and APP (RTC + EEPROM, matrix commands from the fpga, Go io extender).
// Lock order: the SPI bus lock may be held when taking this one, never the other way round:
// release the I2C lock before calling spi_send().

void i2c_bus_init();                    // create the lock (before the tasks start)
void i2c_bus_lock(void);     // also used by lib/RTC
void i2c_bus_unlock(void);

#ifdef __cplusplus
}
#endif
