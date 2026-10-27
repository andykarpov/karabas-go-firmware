#include "ds3231.h"
#include "i2c_dev.h"
#include "rtc_regs.h"

#define DS3231_ADDR 0x68
#define DS3231_REG_TIME 0x00
#define DS3231_REG_STATUS 0x0F
#define DS3231_OSF 0x80

bool ds3231_present(void) {
  return i2c_dev_probe(DS3231_ADDR);
}

bool ds3231_lost_power(void) {
  uint8_t status;
  return i2c_dev_read_reg(DS3231_ADDR, DS3231_REG_STATUS, &status, 1) && (status & DS3231_OSF);
}

bool ds3231_read(kg_datetime_t *t) {
  uint8_t regs[KG_DS3231_TIME_REGS];
  return i2c_dev_read_reg(DS3231_ADDR, DS3231_REG_TIME, regs, sizeof(regs)) && kg_ds3231_decode(regs, t);
}

bool ds3231_write(const kg_datetime_t *t) {
  uint8_t regs[KG_DS3231_TIME_REGS];
  kg_ds3231_encode(t, regs);
  if (!i2c_dev_write_reg(DS3231_ADDR, DS3231_REG_TIME, regs, sizeof(regs))) return false;
  uint8_t status;
  if (!i2c_dev_read_reg(DS3231_ADDR, DS3231_REG_STATUS, &status, 1)) return false;
  status &= ~DS3231_OSF;
  return i2c_dev_write_reg(DS3231_ADDR, DS3231_REG_STATUS, &status, 1);
}
