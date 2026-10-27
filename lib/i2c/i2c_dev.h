#pragma once
// The I2C bus of the board on the pico-sdk (hardware_i2c), blocking transfers with a timeout.
// All devices are fast-mode (400 kHz). The caller holds the I2C bus lock (i2c_bus_lock(),
// src/periph/i2c_bus) around a transfer. Pure C.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// the block (i2c0 / i2c1) follows the pins
void i2c_dev_begin(unsigned sda, unsigned scl, uint32_t hz);
// a device answers its address (a one byte read: the pico-sdk has no address-only transfer)
bool i2c_dev_probe(uint8_t addr);
bool i2c_dev_write(uint8_t addr, const uint8_t *data, size_t len);
bool i2c_dev_read(uint8_t addr, uint8_t *data, size_t len);
// reg, then data in one write (len <= 31)
bool i2c_dev_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len);
// reg, a repeated start, then len bytes
bool i2c_dev_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
