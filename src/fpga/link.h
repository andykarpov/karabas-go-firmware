#pragma once

#include <Arduino.h>
#include "types.h"

// handler for an incoming fpga command (cmd, addr, data frame)
typedef void (*link_handler_t)(uint8_t addr, uint8_t data);

void link_queue_init();
void link_begin();
void link_on(uint8_t cmd, link_handler_t handler);
void link_set_freq(uint8_t mhz);          // core spi frequency, 0 or 255 = default 16 MHz
void link_set_rx_enabled(bool enabled);   // ignore incoming commands while the fpga is being configured

void spi_queue(uint8_t cmd, uint8_t addr, uint8_t data); // enqueue from any core, sent by core0 loop
bool link_queue_pop(queue_spi_t *packet);
void link_report_drops();

void spi_send(uint8_t cmd, uint8_t addr, uint8_t data);
void spi_send16(uint8_t cmd, uint16_t data);
void spi_send24(uint8_t cmd, uint32_t data);
void spi_send32(uint8_t cmd, uint32_t data);
void spi_send64(uint8_t cmd, uint64_t data);
void process_in_cmd(uint8_t cmd, uint8_t addr, uint8_t data);
