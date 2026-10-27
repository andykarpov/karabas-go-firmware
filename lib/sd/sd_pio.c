#include "sd_pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pio_spi.h"

static pio_spi_inst_t spi;
static unsigned pin_sck, pin_mosi, pin_miso;
static uint offset;

static void acquire(void *ctx) { (void) ctx; }
static void release(void *ctx) { (void) ctx; }

static void cs(void *ctx, bool selected) {
  (void) ctx;
  gpio_put(spi.cs_pin, !selected);
}

// the program takes 4 PIO cycles a bit
static void clock(void *ctx, uint32_t hz) {
  (void) ctx;
  float div = (float) clock_get_hz(clk_sys) / ((float) hz * 4);
  if (div < 1.5f) div = 1.5f;
  pio_sm_set_clkdiv(spi.pio, spi.sm, div);
  pio_sm_clkdiv_restart(spi.pio, spi.sm);
}

static void send(void *ctx, const uint8_t *buf, size_t n) {
  (void) ctx;
  pio_spi_write8_blocking(&spi, buf, n);
}

static void recv(void *ctx, uint8_t *buf, size_t n) {
  (void) ctx;
  pio_spi_read8_blocking(&spi, buf, n);
}

bool sd_pio_init(sd_spi_bus_t *bus, unsigned sck, unsigned mosi, unsigned miso, unsigned cs_pin) {
  PIO pio;
  uint sm;
  if (!pio_claim_free_sm_and_add_program(&spi_cpha0_program, &pio, &sm, &offset)) return false;
  spi.pio = pio;
  spi.sm = sm;
  spi.cs_pin = cs_pin;
  pin_sck = sck; pin_mosi = mosi; pin_miso = miso;
  gpio_init(cs_pin);
  gpio_put(cs_pin, 1);
  gpio_set_dir(cs_pin, GPIO_OUT);
  float div = (float) clock_get_hz(clk_sys) / (400000.0f * 4);
  pio_spi_init(pio, sm, offset, 8, div, false, false, sck, mosi, miso);
  bus->acquire = acquire;
  bus->release = release;
  bus->cs = cs;
  bus->clock = clock;
  bus->send = send;
  bus->recv = recv;
  bus->ctx = NULL;
  bus->dedicated = true;
  return true;
}
