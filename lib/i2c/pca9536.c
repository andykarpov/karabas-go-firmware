#include "pca9536.h"
#include "i2c_dev.h"

#define PCA9536_ADDR 0x41
#define PCA9536_REG_INPUT 0x00
#define PCA9536_REG_OUTPUT 0x01
#define PCA9536_REG_CONFIG 0x03 // 1 = input

// a register read (address + W, the pointer, a repeated start): the address byte alone
// (i2c_dev_probe, a one byte read) found no PCA9536 on the Go boards, SparkFun's probe
// was an address + W
bool pca9536_present(void) {
  uint8_t b;
  return i2c_dev_read_reg(PCA9536_ADDR, PCA9536_REG_INPUT, &b, 1);
}

bool pca9536_regs(uint8_t regs[4]) {
  // one at a time: the pointer does not advance by itself
  for (uint8_t i = 0; i < 4; i++) {
    if (!i2c_dev_read_reg(PCA9536_ADDR, i, &regs[i], 1)) return false;
  }
  return true;
}

bool pca9536_outputs(uint8_t mask) {
  uint8_t cfg = ~mask; // bits 7..4 stay 1, as after power on
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
