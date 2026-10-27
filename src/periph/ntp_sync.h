#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The RTC set from NTP through the ESP8266 (BOOT core only, as Setup Wi-Fi). Non-blocking:
// ntp_sync_start(), then ntp_sync_poll() from the tick of the screen.

enum ntp_sync_result_e {
  NTP_IDLE,
  NTP_BUSY,
  NTP_OK,     // the RTC is set
  NTP_ERROR,
};

// tz: the UTC offset in minutes (hw_setup.tz, or the edited one of the graphic setup)
void ntp_sync_start(int tz);
// the screen is left: the reply of the ESP is dropped
void ntp_sync_cancel();
// reads the replies of the ESP, returns the state
uint8_t ntp_sync_poll();
// the state, nothing is read
uint8_t ntp_sync_state();
// the state as text for the screen, NULL when idle
const char *ntp_sync_message();

#ifdef __cplusplus
}
#endif
