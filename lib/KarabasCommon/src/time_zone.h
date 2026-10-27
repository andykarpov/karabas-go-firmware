#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The time zone of the NTP sync (karabas.ini [setup] tz): an offset from UTC in minutes, in
// 15 minute steps, no daylight saving time. The RTC keeps the local time.

#define KG_TZ_MIN (-12 * 60)
#define KG_TZ_MAX (14 * 60)
#define KG_TZ_STEP 15

typedef struct {
  int year, month, day, hour, minute, second;
} kg_datetime_t;

// "+03:00", "-5", "5:30", "0" -> minutes; false if it is not an offset in the range and steps
bool kg_tz_parse(const char *s, int *minutes);
// "+03:00", "-04:30", "+00:00"
void kg_tz_format(int minutes, char *out, size_t size);
// the next (d = 1) / previous (d = -1) offset, round
int kg_tz_step(int minutes, int d);
// t + minutes, over days, months and years
void kg_datetime_add_minutes(kg_datetime_t *t, int minutes);

#ifdef __cplusplus
}
#endif
