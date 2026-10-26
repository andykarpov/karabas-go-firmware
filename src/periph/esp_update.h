#pragma once

#include <Arduino.h>

// The AT firmware of the ESP8266 updated over the air from Espressif's server (AT+CIUPDATE),
// BOOT core only, as Setup Wi-Fi. The module downloads into the other half of its flash and
// boots it only when it is complete: a lost download leaves the old firmware. Non-blocking:
// esp_update_start(), then esp_update_poll() from the tick of the screen.

enum esp_update_result_e {
  ESP_UPD_IDLE,
  ESP_UPD_BUSY,
  ESP_UPD_OK,    // updated, the module answers again
  ESP_UPD_ERROR,
};

void esp_update_start();
// AT+RESTORE: the settings the module keeps in its flash are erased (the network, the mode, the
// UART with its flow control), for modules whose saved settings got broken; it restarts then
void esp_restore_start();
// the screen is left: the module goes on by itself, its replies are dropped
void esp_update_cancel();
// reads the replies of the ESP, returns the state
uint8_t esp_update_poll();
// 0..100, an estimate: the module tells the stages, not the bytes
uint8_t esp_update_progress();
// the stage or the result as text for the screen
const char *esp_update_message();
// the version of the module after the update or the reset ("" before)
const char *esp_update_version();
