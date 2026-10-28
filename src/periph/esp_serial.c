#include "esp_serial.h"
#include <string.h>
#include "kg_ring.h"

#define ESP_SERIAL_RING 4096

static uint8_t rx_buf[ESP_SERIAL_RING], tx_buf[ESP_SERIAL_RING];
static kg_ring_t rx = { rx_buf, ESP_SERIAL_RING, 0, 0 };
static kg_ring_t tx = { tx_buf, ESP_SERIAL_RING, 0, 0 };
static esp_serial_send_t send_byte = NULL;

void esp_serial_begin(esp_serial_send_t send) {
  kg_ring_clear(&rx);
  kg_ring_clear(&tx);
  send_byte = send;
}

void esp_serial_rx_push(uint8_t c) {
  if (send_byte) kg_ring_put(&rx, c);
}

bool esp_serial_handle(void) {
  if (!send_byte) return false;
  int c = kg_ring_get(&tx);
  if (c < 0) return false;
  send_byte((uint8_t) c);
  return true;
}

int esp_serial_read(void) {
  return send_byte ? kg_ring_get(&rx) : -1;
}

int esp_serial_available(void) {
  return send_byte ? (int) kg_ring_count(&rx) : 0;
}

void esp_serial_clear_rx(void) {
  kg_ring_clear(&rx);
}

int esp_serial_available_for_write(void) {
  return send_byte ? (int) kg_ring_free(&tx) : 0;
}

size_t esp_serial_write(const uint8_t *buf, size_t len) {
  if (!send_byte) return 0;
  for (size_t i = 0; i < len; i++) kg_ring_put(&tx, buf[i]);
  return len;
}

void esp_serial_print(const char *s) {
  esp_serial_write((const uint8_t *) s, strlen(s));
}
