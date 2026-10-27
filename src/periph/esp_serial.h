#pragma once

// The uart of the ESP8266 behind the fpga (Mini G): bytes go both ways in CMD_ESP_UART frames,
// bridged by the BOOT core. The received bytes and the bytes to send wait in rings of 4 KB;
// a full ring drops bytes. APP only: the link handler pushes, the main loop sends.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*esp_serial_send_t)(uint8_t c); // one byte to the fpga

void esp_serial_begin(esp_serial_send_t send);
void esp_serial_rx_push(uint8_t c);           // a byte from the ESP (CMD_ESP_UART)
bool esp_serial_handle(void);                 // sends one queued byte; false if there was none

int esp_serial_read(void);                    // -1 if nothing came
int esp_serial_available(void);
void esp_serial_clear_rx(void);

int esp_serial_available_for_write(void);
size_t esp_serial_write(const uint8_t *buf, size_t len);
void esp_serial_print(const char *s);

#ifdef __cplusplus
}
#endif
