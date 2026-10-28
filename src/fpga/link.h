#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// handler for an incoming fpga command (cmd, addr, data frame)
typedef void (*link_handler_t)(uint8_t addr, uint8_t data);

void link_queue_init();
void link_os_init();                      // create the bus lock (before the tasks start)
void link_begin();
void link_on(uint8_t cmd, link_handler_t handler);
void link_set_freq(uint8_t mhz);          // core spi frequency, 0 or 255 = default 16 MHz
void link_set_rx_enabled(bool enabled);   // ignore incoming commands while the fpga is being configured

// SPI0 bus lock (recursive). Hold it around multi-frame sequences
// that must not interleave with other tasks (core config, rom / file transfers).
void spi_bus_lock();
void spi_bus_unlock();
void link_set_dispatch_task(void *task);  // task that runs the incoming command handlers (APP)
void link_set_event_task(void *task);     // task woken by spi_queue (INPUT)
void link_dispatch_pending();             // handle frames received by other tasks
void link_poll();                         // once per main loop: NOP to the fpga if the link was idle

// burst transfers, docs/burst_protocol.md
void link_burst_reset();                  // at a core load: no bursts until the next query
bool link_caps_query();                   // ask the loaded core for its capabilities (CMD_CAPS),
                                          // until it answers (true) or a timeout (an old core)
bool link_burst_available();
#define CAPS_BK 0x08                      // caps bit 3: backup RAM (CMD_BK_MOUNT, CMD_IMG_FLUSH)
uint8_t link_core_caps();                 // the caps of the loaded core (0: no answer)
// send data to `type`'s target starting at addr; returns how many bytes went out as bursts
// (less than len if the fpga stopped being ready: send the rest with classic frames)
size_t link_burst_write(uint8_t type, uint32_t addr, const uint8_t *data, size_t len);
// data for a target of the core (the ROMs, the file loader, ioctl) at addr: bursts of type
// data_cmd if the core takes them, the rest as classic frames: bank_cmd 0..2 the address bits
// 8..31 (before the first byte and at every 256), data_cmd with the low byte of the address
void link_write_data(uint8_t data_cmd, uint8_t bank_cmd, uint32_t addr, const uint8_t *buf, size_t len);

void spi_queue(uint8_t cmd, uint8_t addr, uint8_t data); // enqueue from any core / irq, sent by the input handler
bool link_queue_pop(queue_spi_t *packet);
void link_report_drops();

void spi_send(uint8_t cmd, uint8_t addr, uint8_t data);
void spi_send16(uint8_t cmd, uint16_t data);
void spi_send24(uint8_t cmd, uint32_t data);
void spi_send32(uint8_t cmd, uint32_t data);
void spi_send64(uint8_t cmd, uint64_t data);
void process_in_cmd(uint8_t cmd, uint8_t addr, uint8_t data);

#ifdef __cplusplus
}
#endif
