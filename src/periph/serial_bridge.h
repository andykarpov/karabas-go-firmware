#pragma once

#include <Arduino.h>

void serial_bridge_begin();
void serial_bridge_tick();
void serial_data(uint8_t addr, uint8_t data); // CMD_UART handler
