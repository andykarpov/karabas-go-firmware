#pragma once

// The editing of the clock shared by Setup RTC (app_rtc, text) and the Clock page of the graphic
// setup (app_ft_clock): the date and the time in masked fields, the time zone of NTP sync
// (karabas.ini [setup] tz), writing the RTC, NTP sync and the message of the last action. The
// screens only lay it out, move their focus and map their keys / clicks onto it.

#include <stdbool.h>
#include <stdint.h>
#include "masked_input.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { CE_DATE, CE_TIME, CE_NO_FIELD };                      // the fields
enum { CE_MSG_NONE, CE_MSG_BUSY, CE_MSG_OK, CE_MSG_ERROR };  // the kinds of message

// the fields from the RTC, the time zone from hw_setup, no message, NTP sync stopped
void ce_open(void);
// NTP sync stopped
void ce_close(void);

kg_field_t *ce_field(uint8_t f);
// the fields not edited take the RTC as it runs (the cursor stays)
void ce_follow_rtc(void);
// the fields forget their edits (they follow the RTC again)
void ce_forget_edits(void);
// a digit typed into field f; true when the field is full (the focus may move on)
bool ce_type(uint8_t f, char digit);
// the digit under the cursor of field f one up / down (a joystick or the wheel has no digits)
void ce_turn(uint8_t f, int d);

int16_t ce_tz(void);
void ce_set_tz(int16_t tz);
void ce_tz_step(int d);
// writes the time zone into karabas.ini when it changed; false (and the message) on an error
bool ce_save_tz(void);

// writes the fields to the RTC (and the time zone): false and the message on an error; *bad is
// the invalid field then, CE_NO_FIELD for another error
bool ce_set_clock(uint8_t *bad);
// starts NTP sync with the time zone (saved first)
void ce_sync(void);
// the progress of NTP sync, the timeout of an OK message; true if the screen should redraw
bool ce_tick(void);

// the message and its kind (CE_MSG_*); NULL if none
const char *ce_message(uint8_t *kind);
void ce_clear_message(void);

// the digit of a key, 0 if it is not a digit key
char ce_key_digit(uint8_t key);

#ifdef __cplusplus
}
#endif
