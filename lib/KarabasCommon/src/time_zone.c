#include "time_zone.h"
#include <stdio.h>
#include <string.h>

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

static int days_in_month(int y, int m) {
  static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : days[m - 1];
}

bool kg_datetime_valid(const kg_datetime_t *t) {
  return t->year >= 2000 && t->year <= 2099 && t->month >= 1 && t->month <= 12 &&
         t->day >= 1 && t->day <= days_in_month(t->year, t->month) &&
         t->hour >= 0 && t->hour <= 23 && t->minute >= 0 && t->minute <= 59 &&
         t->second >= 0 && t->second <= 59;
}

int kg_datetime_weekday(const kg_datetime_t *t) {
  long days = days_from_civil(t->year, t->month, t->day); // 1970-01-01 was a Thursday
  return (int) (((days + 4) % 7 + 7) % 7);
}

static bool two_digits(const char *s, int *v) {
  if (!is_digit(s[1])) return false;
  *v = (s[0] == ' ' ? 0 : is_digit(s[0]) ? (s[0] - '0') * 10 : -1000) + (s[1] - '0');
  return *v >= 0;
}

bool kg_datetime_parse_build(const char *date, const char *time, kg_datetime_t *t) {
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  if (strlen(date) != 11 || strlen(time) != 8 || time[2] != ':' || time[5] != ':') return false;
  kg_datetime_t r = {0, 0, 0, 0, 0, 0};
  for (int i = 0; i < 12; i++) {
    if (strncmp(date, months + 3 * i, 3) == 0) r.month = i + 1;
  }
  int c, y;
  if (!r.month || !two_digits(date + 4, &r.day) || !two_digits(date + 7, &c) ||
      !two_digits(date + 9, &y) || !two_digits(time, &r.hour) || !two_digits(time + 3, &r.minute) ||
      !two_digits(time + 6, &r.second)) {
    return false;
  }
  r.year = c * 100 + y;
  if (!kg_datetime_valid(&r)) return false;
  *t = r;
  return true;
}
