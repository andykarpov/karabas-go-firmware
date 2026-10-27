#pragma once
// The DS3231 real time clock (0x68): the time registers through rtc_regs (lib/common).
// The caller holds the I2C bus lock. Pure C.

#include <stdbool.h>
#include "time_zone.h"

#ifdef __cplusplus
extern "C" {
#endif

bool ds3231_present(void);
// the oscillator stopped since the time was set (OSF): the time is not to be trusted
bool ds3231_lost_power(void);
// false on a failed read or a time that is not valid
bool ds3231_read(kg_datetime_t *t);
// sets the time and clears OSF
bool ds3231_write(const kg_datetime_t *t);

#ifdef __cplusplus
}
#endif
