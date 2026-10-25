#pragma once

#include <stdint.h>

// A masked input field of the OSD: "25.10.2026", "14:37". The digits are typed over, the
// cursor stays on the digits and skips the separators.

#define KG_FIELD_MAX 16

typedef struct {
  char text[KG_FIELD_MAX + 1];
  uint8_t len;
  uint8_t pos; // the cursor, always on a digit
} kg_field_t;

// text: the start value with its separators ("01.01.2000")
void kg_field_init(kg_field_t *f, const char *text);
void kg_field_left(kg_field_t *f);
void kg_field_right(kg_field_t *f);
// writes the digit under the cursor and moves to the next one. true if it was the last digit.
bool kg_field_type(kg_field_t *f, char digit);

int kg_days_in_month(int month, int year);
// "DD.MM.YYYY" -> true if it is a date of 2000..2099
bool kg_date_parse(const char *text, int *day, int *month, int *year);
// "HH:MM" -> true if it is a valid time
bool kg_time_parse(const char *text, int *hour, int *minute);
