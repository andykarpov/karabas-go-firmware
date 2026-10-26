#include "main.h"
#include "esp_at_parse.h"
#include "time_zone.h"
#include "periph/ntp_sync.h"

// AT+CIPSNTPCFG with the timezone 0, then AT+CIPSNTPTIME? until the ESP has the time (it gives
// 1970 before its first sync). hw_setup.tz is added here: AT 1.x takes whole hours only. Old AT
// firmware without these commands gets an error, there is no SNTP over UDP of our own.

enum { S_IDLE, S_JAP, S_CFG, S_WAIT, S_TIME };

#define TIME_TRIES 15
#define TIME_WAIT_MS 1000

static uint8_t step = S_IDLE;
static uint8_t result = NTP_IDLE;
static const char *message;
static uint8_t tries;
static ElapsedTimer wait_timer;
static char ssid[KG_WIFI_SSID_MAX + 1];
static kg_datetime_t now;
static bool got_time;
static int tz;

static void on_jap(const char *line) { kg_esp_parse_cwjap(line, ssid, sizeof(ssid)); }
static void on_time(const char *line) { if (kg_esp_parse_sntptime(line, &now)) got_time = true; }

static void finish(uint8_t r, const char *m) {
  step = S_IDLE;
  result = r;
  message = m;
  d_printf("NTP: %s", m); d_println();
}

static void go(uint8_t s) {
  step = s;
  switch (s) {
    case S_JAP:  ssid[0] = '\0'; esp_at_start("AT+CWJAP?", 2000, on_jap); break;
    case S_CFG:  esp_at_start("AT+CIPSNTPCFG=1,0,\"pool.ntp.org\",\"time.google.com\"", 2000, nullptr); break;
    case S_WAIT: wait_timer.reset(); break;
    case S_TIME: got_time = false; esp_at_start("AT+CIPSNTPTIME?", 2000, on_time); break;
  }
}

void ntp_sync_start(int utc_offset) {
  tz = utc_offset;
  tries = 0;
  result = NTP_BUSY;
  message = "Syncing with NTP...";
  go(S_JAP);
}

void ntp_sync_cancel() {
  step = S_IDLE;
  result = NTP_IDLE;
  message = nullptr;
}

static void set_clock() {
  kg_datetime_add_minutes(&now, tz);
  d_printf("NTP: %04d-%02d-%02d %02d:%02d:%02d (UTC%+d min)", now.year, now.month, now.day,
    now.hour, now.minute, now.second, tz); d_println();
  zxrtc.setDay(now.day);
  zxrtc.setMonth(now.month);
  zxrtc.setYear(now.year - 2000);
  zxrtc.setHour(now.hour);
  zxrtc.setMinute(now.minute);
  zxrtc.setSecond(now.second);
  zxrtc.save();
}

uint8_t ntp_sync_poll() {
  if (step == S_IDLE) return result;
  if (step == S_WAIT) {
    if (wait_timer.elapsed() >= TIME_WAIT_MS) go(S_TIME);
    return result;
  }
  uint8_t r = esp_at_poll();
  if (r == ESP_AT_BUSY) return result;
  if (r == ESP_AT_TIMEOUT || r == ESP_AT_IDLE) {
    finish(NTP_ERROR, "No answer from the ESP");
    return result;
  }
  switch (step) {
    case S_JAP:
      // "No AP" when not joined
      if (r != ESP_AT_OK || !ssid[0]) finish(NTP_ERROR, "Wi-Fi is not connected");
      else go(S_CFG);
      break;
    case S_CFG:
      if (r != ESP_AT_OK) finish(NTP_ERROR, "No NTP in the ESP firmware");
      else go(S_WAIT);
      break;
    case S_TIME:
      if (got_time) { set_clock(); finish(NTP_OK, "The clock is set from NTP"); }
      else if (++tries >= TIME_TRIES) finish(NTP_ERROR, "No time from NTP");
      else go(S_WAIT);
      break;
  }
  return result;
}

uint8_t ntp_sync_state() {
  return result;
}

const char *ntp_sync_message() {
  return message;
}
