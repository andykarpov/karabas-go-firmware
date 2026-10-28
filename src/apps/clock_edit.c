#include "main.h"
#include "apps/clock_edit.h"
#include "apps/app_ft_setup.h"
#include "input/usb_hid_keys.h"
#include "time_zone.h"

#define MESSAGE_MS 5000 // an OK message goes after a while

static kg_field_t fields[2];
static bool edited[2];
static int16_t tz;
static const char *message;
static uint8_t message_kind = CE_MSG_NONE;
static uint32_t message_timer = 0;

static void say(const char *msg, uint8_t kind) {
  message = msg;
  message_kind = msg ? kind : CE_MSG_NONE;
  message_timer = ms_now();
}

void ce_open(void) {
  ce_forget_edits();
  fields[CE_DATE].pos = fields[CE_TIME].pos = 0;
  ce_follow_rtc();
  tz = hw_setup.tz;
  ntp_sync_cancel();
  say(NULL, CE_MSG_NONE);
}

void ce_close(void) {
  ntp_sync_cancel();
}

kg_field_t *ce_field(uint8_t f) { return &fields[f]; }

static void from_rtc(uint8_t f, const char *text) {
  uint8_t pos = fields[f].pos;
  kg_field_init(&fields[f], text);
  fields[f].pos = pos < fields[f].len ? pos : 0;
}

void ce_follow_rtc(void) {
  char buf[11];
  if (!edited[CE_DATE]) {
    snprintf(buf, sizeof(buf), "%02u.%02u.%04u", rtc_get_day(), rtc_get_month(), 2000 + rtc_get_year());
    from_rtc(CE_DATE, buf);
  }
  if (!edited[CE_TIME]) {
    snprintf(buf, sizeof(buf), "%02u:%02u", rtc_get_hour(), rtc_get_minute());
    from_rtc(CE_TIME, buf);
  }
}

void ce_forget_edits(void) {
  edited[CE_DATE] = edited[CE_TIME] = false;
}

static void edit(uint8_t f) {
  edited[f] = true;
  if (message_kind != CE_MSG_BUSY) say(NULL, CE_MSG_NONE);
}

bool ce_type(uint8_t f, char digit) {
  edit(f);
  return kg_field_type(&fields[f], digit);
}

void ce_turn(uint8_t f, int d) {
  edit(f);
  kg_field_turn(&fields[f], d);
}

int16_t ce_tz(void) { return tz; }
void ce_set_tz(int16_t t) { tz = t; }
void ce_tz_step(int d) { tz = kg_tz_step(tz, d); }

bool ce_save_tz(void) {
  if (tz == hw_setup.tz) return true;
  setup_t n = hw_setup;
  n.tz = tz;
  if (!save_setup(&n)) {
    say("Can't save karabas.ini", CE_MSG_ERROR);
    return false;
  }
  app_ft_setup_tz_saved(tz); // the graphic setup keeps a copy
  return true;
}

bool ce_set_clock(uint8_t *bad) {
  int day, month, year, hour, minute;
  *bad = CE_NO_FIELD;
  ntp_sync_cancel();
  if (!kg_date_parse(fields[CE_DATE].text, &day, &month, &year)) {
    *bad = CE_DATE;
    say("Invalid date", CE_MSG_ERROR);
    return false;
  }
  if (!kg_time_parse(fields[CE_TIME].text, &hour, &minute)) {
    *bad = CE_TIME;
    say("Invalid time", CE_MSG_ERROR);
    return false;
  }
  if (!ce_save_tz()) return false;
  rtc_set_day(day);
  rtc_set_month(month);
  rtc_set_year(year - 2000);
  rtc_set_hour(hour);
  rtc_set_minute(minute);
  rtc_set_second(0);
  rtc_save();
  ce_forget_edits();
  say("The clock is set", CE_MSG_OK);
  return true;
}

void ce_sync(void) {
  if (!ce_save_tz()) return;
  ntp_sync_start(tz);
  say(ntp_sync_message(), CE_MSG_BUSY);
}

bool ce_tick(void) {
  if (ntp_sync_state() == NTP_BUSY) {
    uint8_t r = ntp_sync_poll();
    if (r == NTP_BUSY) return false;
    if (r == NTP_OK) {
      // the RTC as NTP set it
      ce_forget_edits();
      ce_follow_rtc();
    }
    say(ntp_sync_message(), r == NTP_OK ? CE_MSG_OK : CE_MSG_ERROR);
    return true;
  }
  if (message_kind != CE_MSG_OK || ms_since(message_timer) < MESSAGE_MS) return false;
  say(NULL, CE_MSG_NONE);
  return true;
}

const char *ce_message(uint8_t *kind) {
  if (kind) *kind = message_kind;
  return message_kind == CE_MSG_BUSY ? ntp_sync_message() : message; // the progress as it goes
}

void ce_clear_message(void) {
  if (message_kind != CE_MSG_BUSY) say(NULL, CE_MSG_NONE);
}

char ce_key_digit(uint8_t key) {
  if (key >= KEY_1 && key <= KEY_9) return '1' + (key - KEY_1);
  if (key == KEY_0) return '0';
  if (key >= KEY_KP1 && key <= KEY_KP9) return '1' + (key - KEY_KP1);
  if (key == KEY_KP0) return '0';
  return 0;
}
