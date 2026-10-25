#pragma once

#include <Arduino.h>

// Non-blocking AT commands to the ESP8266 behind the FPGA (esp_serial, CMD_ESP_UART; only the
// BOOT core bridges it). The bytes come and go in the main loop (link dispatch and
// esp_serial.handle()), so a command is started and then polled from a tick: the reply lines
// go to a callback until OK, ERROR / FAIL or the timeout.

enum esp_at_result_e {
  ESP_AT_IDLE,    // nothing started yet
  ESP_AT_BUSY,
  ESP_AT_OK,
  ESP_AT_ERROR,   // ERROR or FAIL
  ESP_AT_TIMEOUT,
};

typedef void (*esp_at_line_cb)(const char *line);

// sends "<cmd>\r\n" after dropping what the ESP sent before; on_line may be nullptr
void esp_at_start(const char *cmd, uint32_t timeout_ms, esp_at_line_cb on_line);
// reads the reply lines that came, returns the state of the last command
uint8_t esp_at_poll();
// a command is waiting for its reply (false once its timeout passed, even if nobody polls)
bool esp_at_busy();
