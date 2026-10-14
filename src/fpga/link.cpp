#include "main.h"

// MCU <-> FPGA link over SPI0: 3-byte frames (cmd, addr, data). The FPGA answers
// with its own frame in the same transfer; non-zero answers go to registered handlers.

static SPISettings settingsA(SD_SCK_MHZ(16), MSBFIRST, SPI_MODE0); // default MCU SPI settings
static uint8_t link_freq = 0; // core spi frequency in MHz, 0 or 255 = default
static bool link_rx_enabled = true;
static link_handler_t handlers[256] = { nullptr };

// events from core1 (usb hid, ps/2) to core0. sized to survive a core load (a few seconds) without drops
#define SPI_EVENT_QUEUE_SIZE 512
static queue_t spi_event_queue;
static volatile uint32_t spi_queue_dropped = 0;

static uint16_t debug_address = 0;
static uint16_t debug_data = 0;

void link_queue_init() {
  queue_init(&spi_event_queue, sizeof(queue_spi_t), SPI_EVENT_QUEUE_SIZE);
}

void link_begin() {
  SPI.setSCK(PIN_MCU_SPI_SCK);
  SPI.setRX(PIN_MCU_SPI_RX);
  SPI.setTX(PIN_MCU_SPI_TX);
  //SPI.setCS(PIN_MCU_SPI_CS);
  SPI.begin(false);

  link_on(CMD_DEBUG_ADDRESS, [](uint8_t addr, uint8_t data) {
    uint16_t val = addr*256+data;
    if (val != debug_address) { debug_address = val; d_printf("Debug Address: %04x", debug_address); d_println(); }
  });
  link_on(CMD_DEBUG_DATA, [](uint8_t addr, uint8_t data) {
    uint16_t val = addr*256+data;
    if (val != debug_data) { debug_data = val; d_printf("Debug Data: %04x", debug_data); d_println(); }
  });
}

void link_on(uint8_t cmd, link_handler_t handler) {
  handlers[cmd] = handler;
}

void link_set_freq(uint8_t mhz) {
  link_freq = mhz;
}

void link_set_rx_enabled(bool enabled) {
  link_rx_enabled = enabled;
}

void process_in_cmd(uint8_t cmd, uint8_t addr, uint8_t data) {
  if (handlers[cmd] != nullptr) {
    handlers[cmd](addr, data);
  }
}

void spi_queue(uint8_t cmd, uint8_t addr, uint8_t data) {
  queue_spi_t packet;
  packet.cmd = cmd;
  packet.addr = addr;
  packet.data = data;
  if (!queue_try_add(&spi_event_queue, &packet)) {
    spi_queue_dropped++;
  }
}

bool link_queue_pop(queue_spi_t *packet) {
  return queue_try_remove(&spi_event_queue, packet);
}

void link_report_drops() {
  static uint32_t prev_spi_queue_dropped = 0;
  if (spi_queue_dropped != prev_spi_queue_dropped) {
    prev_spi_queue_dropped = spi_queue_dropped;
    d_printf("SPI event queue overflow, dropped: %lu", (unsigned long) spi_queue_dropped); d_println();
  }
}

void spi_send(uint8_t cmd, uint8_t addr, uint8_t data) {
  // use default (16 MHz) or custom spi freq from the core config
  SPISettings spi_settings = (link_freq == 0 || link_freq == 255) ? settingsA : SPISettings(SD_SCK_MHZ(link_freq), MSBFIRST, SPI_MODE0);
  SPI.beginTransaction(spi_settings);
  // one 3-byte transfer keeps the SPI FIFO busy instead of 3 separate blocking calls
  const uint8_t tx[3] = {cmd, addr, data};
  uint8_t rx[3];
  gpio_put(PIN_MCU_SPI_CS, LOW);
  SPI.transfer(tx, rx, sizeof(tx));
  gpio_put(PIN_MCU_SPI_CS, HIGH);
  SPI.endTransaction();
  if ((rx[0] > 0) && link_rx_enabled) {
    process_in_cmd(rx[0], rx[1], rx[2]);
  }
}

// multi-byte values are sent as consecutive addr 0..n-1, least significant byte first
static void spi_send_le(uint8_t cmd, uint64_t data, uint8_t len) {
  for (uint8_t i=0; i<len; i++) {
    spi_send(cmd, i, (uint8_t)(data >> (8*i)));
  }
}

void spi_send16(uint8_t cmd, uint16_t data) { spi_send_le(cmd, data, 2); }
void spi_send24(uint8_t cmd, uint32_t data) { spi_send_le(cmd, data, 3); }
void spi_send32(uint8_t cmd, uint32_t data) { spi_send_le(cmd, data, 4); }
void spi_send64(uint8_t cmd, uint64_t data) { spi_send_le(cmd, data, 8); }
