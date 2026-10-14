#include "main.h"

// USB CDC serial <-> core UART bridge (TS ZiFi and ZX Evo RS232)

static uint8_t uart_idx = 0;
static uint8_t evo_rs232_dll = 0;
static uint8_t evo_rs232_dlm = 0;
static uint32_t serial_speed = 115200;
static unsigned long uart_rx_last = 0;

void serial_set_speed(uint8_t dll, uint8_t dlm) {
  uint32_t speed = 0;
  if (dll == 0 && dlm == 0) {
    speed = 256000; // zx evo special case
  } else if (bitRead(dlm, 7) == 1) {
    // native atmega mode
    dlm = bitClear(dlm, 7);
    // (uint16)((DLM&0x7F)*256+DLL) = (691200/<скорость в бодах>)-1 
    speed = 691200 / ((dlm*256) + dll + 1);
  } else {
    // standard mode
    speed = 115200 / ((dlm*256) + dll);
  }
  // switch serial spseed
  if (serial_speed != speed) {
    serial_speed = speed;
    Serial.end();
    Serial.begin(speed);
  }
}

void serial_data(uint8_t addr, uint8_t data) {
    if (addr == 0) {
      // ts zifi 115200
      if (serial_speed != 115200) {
        serial_speed = 115200;
        Serial.end();
        Serial.begin(serial_speed);
      }
      Serial.write(data);
    } else if (addr == 1) {
      // evo rs232 dll
      evo_rs232_dll = data;
      serial_set_speed(evo_rs232_dll, evo_rs232_dlm);
    } else if (addr == 2) {
      // evo rs232 set dlm
      evo_rs232_dlm = data;
      serial_set_speed(evo_rs232_dll, evo_rs232_dlm);
    } else if (addr == 3) {
      // evo rs232 data
      Serial.write(data);
    }
}

void serial_bridge_begin() {
  Serial.begin(serial_speed);
}

// forward bytes received from the host to the core
void serial_bridge_tick() {
  if (Serial.available() > 0) {
    // receive at least on the serial_speed
    unsigned long uart_rx_delay = 30 * 1e6 / serial_speed;
    if (micros() - uart_rx_last >= uart_rx_delay) {
      uart_idx++;
      uart_rx_last = micros();
      int uart_rx = Serial.read();
      if (uart_rx != -1) {
        spi_send(CMD_UART, uart_idx, (uint8_t) uart_rx);
      }
    }
  }
}
