#include "masked_input.h"
#include <string.h>

static bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

void kg_field_init(kg_field_t *f, const char *text) {
  size_t len = strlen(text);
  if (len > KG_FIELD_MAX) len = KG_FIELD_MAX;
  memcpy(f->text, text, len);
  f->text[len] = '\0';
  f->len = (uint8_t)len;
  f->pos = 0;
  while (f->pos < f->len && !is_digit(f->text[f->pos])) f->pos++;
}

void kg_field_left(kg_field_t *f) {
  int p = f->pos - 1;
  while (p >= 0 && !is_digit(f->text[p])) p--;
  if (p >= 0) f->pos = (uint8_t)p;
}

void kg_field_right(kg_field_t *f) {
  int p = f->pos + 1;
  while (p < f->len && !is_digit(f->text[p])) p++;
  if (p < f->len) f->pos = (uint8_t)p;
}

bool kg_field_type(kg_field_t *f, char digit) {
  if (!is_digit(digit) || f->pos >= f->len) return false;
  f->text[f->pos] = digit;
  uint8_t was = f->pos;
  kg_field_right(f);
  return f->pos == was;
}

void kg_field_turn(kg_field_t *f, int d) {
  if (f->pos >= f->len) return;
  char *ch = &f->text[f->pos];
  if (!is_digit(*ch)) return;
  *ch = '0' + ((*ch - '0' + d) % 10 + 10) % 10;
}

void kg_text_add(char *text, size_t max) {
  size_t len = strlen(text);
  if (len >= max) return;
  text[len] = KG_TEXT_CHARS[0];
  text[len + 1] = '\0';
}

void kg_text_turn(char *text, size_t max, int d) {
  size_t len = strlen(text);
  if (len == 0) { kg_text_add(text, max); return; }
  static const char chars[] = KG_TEXT_CHARS;
  const int n = sizeof(chars) - 1;
  const char *p = strchr(chars, text[len - 1]);
  int i = p && text[len - 1] ? (int)(p - chars) : 0;
  text[len - 1] = chars[((i + d) % n + n) % n];
}

// n digits at text, -1 if one is not a digit
static int number(const char *text, int n) {
  int v = 0;
  for (int i = 0; i < n; i++) {
    if (!is_digit(text[i])) return -1;
    v = v * 10 + (text[i] - '0');
  }
  return v;
}

int kg_days_in_month(int month, int year) {
  static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return days[month - 1] + (month == 2 && leap ? 1 : 0);
}

bool kg_date_parse(const char *text, int *day, int *month, int *year) {
  if (strlen(text) != 10 || text[2] != '.' || text[5] != '.') return false;
  int d = number(text, 2), m = number(text + 3, 2), y = number(text + 6, 4);
  if (y < 2000 || y > 2099) return false;
  if (d < 1 || d > kg_days_in_month(m, y)) return false;
  *day = d;
  *month = m;
  *year = y;
  return true;
}

bool kg_time_parse(const char *text, int *hour, int *minute) {
  if (strlen(text) != 5 || text[2] != ':') return false;
  int h = number(text, 2), m = number(text + 3, 2);
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  *hour = h;
  *minute = m;
  return true;
}
