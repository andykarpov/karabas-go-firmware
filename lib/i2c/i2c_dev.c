#include "i2c_dev.h"
#include <string.h>
#include "hardware/gpio.h"
#include "hardware/i2c.h"

static i2c_inst_t *bus = i2c0;

// a byte is 22.5 us at 400 kHz; a device may stretch the clock a little
static uint32_t timeout_us(size_t len) {
  return 2000 + 50 * (uint32_t) len;
}

void i2c_dev_begin(unsigned sda, unsigned scl, uint32_t hz) {
  bus = ((sda >> 1) & 1) ? i2c1 : i2c0;
  i2c_init(bus, hz);
  gpio_set_function(sda, GPIO_FUNC_I2C);
  gpio_set_function(scl, GPIO_FUNC_I2C);
  gpio_pull_up(sda);
  gpio_pull_up(scl);
}

bool i2c_dev_probe(uint8_t addr) {
  uint8_t b;
  return i2c_read_timeout_us(bus, addr, &b, 1, false, timeout_us(1)) == 1;
}

bool i2c_dev_write(uint8_t addr, const uint8_t *data, size_t len) {
  return i2c_write_timeout_us(bus, addr, data, len, false, timeout_us(len)) == (int) len;
}

bool i2c_dev_read(uint8_t addr, uint8_t *data, size_t len) {
  return i2c_read_timeout_us(bus, addr, data, len, false, timeout_us(len)) == (int) len;
}

bool i2c_dev_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len) {
  uint8_t buf[32];
  if (len > sizeof(buf) - 1) return false;
  buf[0] = reg;
  memcpy(buf + 1, data, len);
  return i2c_dev_write(addr, buf, len + 1);
}

bool i2c_dev_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, size_t len) {
  if (i2c_write_timeout_us(bus, addr, &reg, 1, true, timeout_us(1)) != 1) return false;
  return i2c_dev_read(addr, data, len);
}
