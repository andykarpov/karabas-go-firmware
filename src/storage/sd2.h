#pragma once

#include <Print.h>
#include <stdint.h>

// The second SD card: the slot of the FPGA. The boot core passes it through to the MCU link
// (kg_mcu ENABLE_SD2: SCK / MOSI / MISO of SPI0, chip select PIN_MCU_SD2_CS), so it is reached
// only while the boot core runs. Every call holds the SPI bus lock.

bool sd2_begin();                 // sets the card up (again)
uint32_t sd2_sector_count();
bool sd2_read(uint32_t sector, uint8_t *dst, uint32_t count);
bool sd2_write(uint32_t sector, const uint8_t *src, uint32_t count);
void sd2_sync();

// AT+KARABAS=sd2test: card info, then reads at a few SPI clocks, with a link frame between the
// blocks, so the clock changes as it does in use. Nothing is written.
void sd2_test(Print &out);
