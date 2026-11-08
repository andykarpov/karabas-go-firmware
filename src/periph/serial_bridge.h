#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void serial_bridge_begin();
void serial_bridge_tick();
void serial_data(uint8_t addr, uint8_t data); // CMD_UART handler
// to the CDC port (APP only): the answers of the AT+KARABAS commands
void serial_host_print(const char *s);
void serial_host_flush(void);

#ifdef __cplusplus
}
#endif
