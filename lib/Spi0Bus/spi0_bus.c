#include "spi0_bus.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

static uint32_t clock_hz = 0;

void spi0_bus_init(unsigned sck, unsigned tx, unsigned rx) {
  clock_hz = 16000000;
  spi_init(spi0, clock_hz);
  spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  gpio_set_function(sck, GPIO_FUNC_SPI);
  gpio_set_function(tx, GPIO_FUNC_SPI);
  gpio_set_function(rx, GPIO_FUNC_SPI);
}

void spi0_bus_clock(uint32_t hz) {
  if (hz == clock_hz) return;
  spi_set_baudrate(spi0, hz);
  clock_hz = hz;
}

void spi0_bus_forget_clock(void) {
  clock_hz = 0;
}

void spi0_bus_xfer(const uint8_t *tx, uint8_t *rx, size_t len) {
  if (rx) spi_write_read_blocking(spi0, tx, rx, len);
  else spi_write_blocking(spi0, tx, len);
}

uint8_t spi0_bus_byte(uint8_t tx) {
  uint8_t rx;
  spi_write_read_blocking(spi0, &tx, &rx, 1);
  return rx;
}
