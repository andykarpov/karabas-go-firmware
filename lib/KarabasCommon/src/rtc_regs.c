#include "rtc_regs.h"

uint8_t kg_bin2bcd(uint8_t v) {
  return v + 6 * (v / 10);
}

uint8_t kg_bcd2bin(uint8_t v) {
  return v - 6 * (v >> 4);
}

void kg_ds3231_encode(const kg_datetime_t *t, uint8_t regs[KG_DS3231_TIME_REGS]) {
  int wd = kg_datetime_weekday(t);
  regs[0] = kg_bin2bcd(t->second);
  regs[1] = kg_bin2bcd(t->minute);
  regs[2] = kg_bin2bcd(t->hour);
  regs[3] = kg_bin2bcd(wd == 0 ? 7 : wd);
  regs[4] = kg_bin2bcd(t->day);
  regs[5] = kg_bin2bcd(t->month);
  regs[6] = kg_bin2bcd(t->year - 2000);
}

bool kg_ds3231_decode(const uint8_t regs[KG_DS3231_TIME_REGS], kg_datetime_t *t) {
  kg_datetime_t r;
  r.second = kg_bcd2bin(regs[0] & 0x7F);
  r.minute = kg_bcd2bin(regs[1] & 0x7F);
  r.hour = kg_bcd2bin(regs[2] & 0x3F); // 24 h mode: bit 6 is clear
  r.day = kg_bcd2bin(regs[4] & 0x3F);
  r.month = kg_bcd2bin(regs[5] & 0x1F); // bit 7: the century
  r.year = 2000 + kg_bcd2bin(regs[6]);
  if ((regs[2] & 0x40) || !kg_datetime_valid(&r)) return false;
  *t = r;
  return true;
}
