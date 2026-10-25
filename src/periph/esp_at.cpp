#include "main.h"
#include "esp_at_parse.h"
#include "periph/esp_at.h"

static uint8_t state = ESP_AT_IDLE;
static esp_at_line_cb line_cb;
static uint32_t timeout;
static ElapsedTimer timer;
static char line[160]; // a longer line is cut
static uint8_t line_len;

void esp_at_start(const char *cmd, uint32_t timeout_ms, esp_at_line_cb on_line) {
  esp_serial.rx_fifo.clear();
  line_len = 0;
  line_cb = on_line;
  timeout = timeout_ms;
  timer.reset();
  state = ESP_AT_BUSY;
  // the password stays out of the log
  if (strncmp(cmd, "AT+CWJAP=", 9) == 0) { d_println("ESP >> AT+CWJAP=..."); }
  else { d_printf("ESP >> %s", cmd); d_println(); }
  esp_serial.print(cmd);
  esp_serial.print("\r\n");
}

bool esp_at_busy() {
  if (state == ESP_AT_BUSY && timer.elapsed() >= timeout) {
    d_println("ESP: timeout");
    state = ESP_AT_TIMEOUT;
  }
  return state == ESP_AT_BUSY;
}

static void on_line() {
  line[line_len] = '\0';
  line_len = 0;
  if (line[0] == '\0') return;
  d_printf("ESP << %s", line); d_println();
  switch (kg_esp_line(line)) {
    case KG_ESP_LINE_OK: state = ESP_AT_OK; break;
    case KG_ESP_LINE_ERROR: state = ESP_AT_ERROR; break;
    default: if (line_cb) line_cb(line);
  }
}

uint8_t esp_at_poll() {
  while (state == ESP_AT_BUSY && esp_serial.available() > 0) {
    char c = (char) esp_serial.read();
    if (c == '\r' || c == '\n') {
      on_line();
    } else if (c != '\0' && line_len < sizeof(line) - 1) {
      line[line_len++] = c;
    }
  }
  esp_at_busy(); // timeout
  return state;
}
