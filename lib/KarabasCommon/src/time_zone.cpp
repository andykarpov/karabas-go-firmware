#include "time_zone.h"
#include <stdio.h>

static bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

bool kg_tz_parse(const char *s, int *minutes) {
  int sign = 1;
  if (*s == '+' || *s == '-') sign = *s++ == '-' ? -1 : 1;
  if (!is_digit(*s)) return false;
  int h = *s++ - '0';
  if (is_digit(*s)) h = h * 10 + (*s++ - '0');
  int m = 0;
  if (*s == ':') {
    s++;
    if (!is_digit(s[0]) || !is_digit(s[1])) return false;
    m = (s[0] - '0') * 10 + (s[1] - '0');
    s += 2;
  }
  if (*s != '\0' || m >= 60 || m % KG_TZ_STEP != 0) return false;
  int v = sign * (h * 60 + m);
  if (v < KG_TZ_MIN || v > KG_TZ_MAX) return false;
  *minutes = v;
  return true;
}

void kg_tz_format(int minutes, char *out, size_t size) {
  int a = minutes < 0 ? -minutes : minutes;
  snprintf(out, size, "%c%02d:%02d", minutes < 0 ? '-' : '+', a / 60, a % 60);
}

int kg_tz_step(int minutes, int d) {
  int n = (KG_TZ_MAX - KG_TZ_MIN) / KG_TZ_STEP + 1;
  int i = (minutes - KG_TZ_MIN) / KG_TZ_STEP;
  i = ((i + d) % n + n) % n;
  return KG_TZ_MIN + i * KG_TZ_STEP;
}

// days since 1970-01-01 and back (proleptic Gregorian, H. Hinnant's algorithms)
static long days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static void civil_from_days(long z, int *y, int *m, int *d) {
  z += 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  long doe = z - era * 146097;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = yoe + era * 400 + (*m <= 2);
}

void kg_datetime_add_minutes(kg_datetime_t *t, int minutes) {
  long m = t->hour * 60L + t->minute + minutes;
  long days = days_from_civil(t->year, t->month, t->day);
  long dd = m >= 0 ? m / 1440 : -((-m + 1439) / 1440);
  m -= dd * 1440;
  civil_from_days(days + dd, &t->year, &t->month, &t->day);
  t->hour = m / 60;
  t->minute = m % 60;
}
