#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A USB-UART adapter on the USB host (CDC ACM, FTDI, CP210x, CH34x: TinyUSB cdc_host). While
// one is plugged, the serial bridge sends the core UART (CMD_UART) to it instead of the CDC
// port of the board, which keeps the log and AT+KARABAS. Only the first adapter is used.
// 8N1, the speed of the core (115200 by default).

// APP side
bool usb_uart_active(void);            // an adapter is mounted
void usb_uart_write(uint8_t data);     // a byte of the core to the adapter (lost when full)
int usb_uart_read(void);               // a byte from the adapter, -1 if none
void usb_uart_set_baud(uint32_t baud); // the speed of the core

// USB task side: from the USB loop (runs from RAM, cheap when idle)
void usb_uart_host_tick(void);

typedef struct {
  bool mounted;
  uint32_t baud;
  uint32_t rx, tx;                 // bytes from / to the adapter
  uint32_t rx_lost, tx_lost;       // the rings were full
} usb_uart_stats_t;
void usb_uart_stats(usb_uart_stats_t *stats);

#ifdef __cplusplus
}
#endif
