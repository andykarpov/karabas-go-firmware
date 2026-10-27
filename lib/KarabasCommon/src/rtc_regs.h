#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "time_zone.h"

#ifdef __cplusplus
extern "C" {
#endif

// The time registers 00..06 of the DS3231 (seconds, minutes, hours, day of the week, date,
// month / century, year), 24 h, the years 2000..2099. As RTClib: the day of the week is
// 1..7 from Monday (7 = Sunday) and comes from the date, the century bit is left alone.

#define KG_DS3231_TIME_REGS 7

uint8_t kg_bin2bcd(uint8_t v);
uint8_t kg_bcd2bin(uint8_t v);

void kg_ds3231_encode(const kg_datetime_t *t, uint8_t regs[KG_DS3231_TIME_REGS]);
// false if the registers are not a valid time (a chip that lost its time, a failed read)
bool kg_ds3231_decode(const uint8_t regs[KG_DS3231_TIME_REGS], kg_datetime_t *t);

#ifdef __cplusplus
}
#endif
