#include "eeprom24.h"
#include "i2c_dev.h"
#include "pico/time.h"

#define EEPROM24_ADDR 0x50
#define EEPROM24_WRITE_US 10000 // a write cycle is 5 ms at most

bool eeprom24_present(void) {
  return i2c_dev_probe(EEPROM24_ADDR);
}

// the chip does not answer while it writes
static bool wait_ready(void) {
  uint32_t start = time_us_32();
  while (!i2c_dev_probe(EEPROM24_ADDR)) {
    if (time_us_32() - start > EEPROM24_WRITE_US) return false;
    busy_wait_us_32(100);
  }
  return true;
}

static uint8_t dev_addr(uint16_t addr) {
  return EEPROM24_ADDR | ((addr >> 8) & 0x03);
}

uint8_t eeprom24_read(uint16_t addr) {
  uint8_t val = 0xFF;
  if (addr >= EEPROM24_SIZE || !wait_ready()) return 0xFF;
  if (!i2c_dev_read_reg(dev_addr(addr), addr & 0xFF, &val, 1)) return 0xFF;
  return val;
}

bool eeprom24_write(uint16_t addr, uint8_t val) {
  if (addr >= EEPROM24_SIZE) return false;
  if (eeprom24_read(addr) == val) return true;
  return wait_ready() && i2c_dev_write_reg(dev_addr(addr), addr & 0xFF, &val, 1);
}
