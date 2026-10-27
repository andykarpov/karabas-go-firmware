#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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
// the digit under the cursor + d, 0..9 round (a joystick, the mouse wheel)
void kg_field_turn(kg_field_t *f, int d);

// A free text field edited with a joystick: the last char is turned through KG_TEXT_CHARS.
#define KG_TEXT_CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" \
                      " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"
// the last char + d in KG_TEXT_CHARS (round); an empty text gets the first char
void kg_text_turn(char *text, size_t max, int d);
// adds the first char of KG_TEXT_CHARS if the text is shorter than max
void kg_text_add(char *text, size_t max);

int kg_days_in_month(int month, int year);
// "DD.MM.YYYY" -> true if it is a date of 2000..2099
bool kg_date_parse(const char *text, int *day, int *month, int *year);
// "HH:MM" -> true if it is a valid time
bool kg_time_parse(const char *text, int *hour, int *minute);

#ifdef __cplusplus
}
#endif
