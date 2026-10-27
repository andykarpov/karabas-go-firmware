#include "pca9536.h"
#include "i2c_dev.h"

#define PCA9536_ADDR 0x41
#define PCA9536_REG_INPUT 0x00
#define PCA9536_REG_OUTPUT 0x01
#define PCA9536_REG_CONFIG 0x03 // 1 = input

bool pca9536_present(void) {
  return i2c_dev_probe(PCA9536_ADDR);
}

bool pca9536_outputs(uint8_t mask) {
  uint8_t cfg = ~mask & 0x0F;
  return i2c_dev_write_reg(PCA9536_ADDR, PCA9536_REG_CONFIG, &cfg, 1);
}

bool pca9536_write(uint8_t pin, bool high) {
  uint8_t out;
  if (pin > 3 || !i2c_dev_read_reg(PCA9536_ADDR, PCA9536_REG_OUTPUT, &out, 1)) return false;
  out = high ? out | (1 << pin) : out & ~(1 << pin);
  return i2c_dev_write_reg(PCA9536_ADDR, PCA9536_REG_OUTPUT, &out, 1);
}

bool pca9536_read(uint8_t *levels) {
  if (!i2c_dev_read_reg(PCA9536_ADDR, PCA9536_REG_INPUT, levels, 1)) return false;
  *levels &= 0x0F;
  return true;
}
