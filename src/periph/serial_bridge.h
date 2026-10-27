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

#ifdef __cplusplus
}
#endif
