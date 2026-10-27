#pragma once
// SPI0 on the pico-sdk: the FPGA link, the FT812 and the second SD card share it, each at its
// own clock, mode 0, MSB first. The caller holds the SPI bus lock (spi_bus_lock) around a
// transfer and drives its chip select. A clock change only sets the baud divider (no reset of
// the SPI block). Pure C.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void spi0_bus_init(unsigned sck, unsigned tx, unsigned rx);
void spi0_bus_clock(uint32_t hz);  // before a transfer: the clock of this device
void spi0_bus_xfer(const uint8_t *tx, uint8_t *rx, size_t len); // rx NULL: write only
uint8_t spi0_bus_byte(uint8_t tx);
void spi0_bus_read(uint8_t *rx, size_t len); // sends 0xFF

#ifdef __cplusplus
}
#endif
