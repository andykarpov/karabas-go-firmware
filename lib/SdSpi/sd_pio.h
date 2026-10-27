#pragma once
// The SPI of SD1 on a PIO state machine (spi.pio, mode 0) with a GPIO chip select: a
// dedicated bus for sd_card. The program goes into a PIO with a free state machine and room
// (pio0: pio1 belongs to the USB host). Pure C.

#include <stdbool.h>
#include "sd_card.h"

#ifdef __cplusplus
extern "C" {
#endif

// false if no PIO has a free state machine and room for the program
bool sd_pio_init(sd_spi_bus_t *bus, unsigned sck, unsigned mosi, unsigned miso, unsigned cs);

#ifdef __cplusplus
}
#endif
