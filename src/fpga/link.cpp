#include "main.h"

#ifdef __FREERTOS
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#endif

// MCU <-> FPGA link over SPI0: 3-byte frames (cmd, addr, data). The FPGA answers
// with its own frame in the same transfer; non-zero answers go to registered handlers.
//
// FreeRTOS build: SPI0 is shared by several tasks (and the FT812 library), so every transfer
// holds the recursive spi bus lock. Incoming commands are always handled in the dispatch task
// (APP): frames received by other tasks are queued and handled by link_dispatch_pending().
// Without FreeRTOS everything runs in the main loop, the lock is a no-op and frames are
// handled immediately.

static SPISettings settingsA(SD_SCK_MHZ(16), MSBFIRST, SPI_MODE0); // default MCU SPI settings
static uint8_t link_freq = 0; // core spi frequency in MHz, 0 or 255 = default
static bool link_rx_enabled = true;
static link_handler_t handlers[256] = { nullptr };

// events from core1 (usb hid, ps/2) to core0. sized to survive a core load (a few seconds) without drops
#define SPI_EVENT_QUEUE_SIZE 512
static queue_t spi_event_queue;
static volatile uint32_t spi_queue_dropped = 0;

// incoming frames received outside the dispatch task
#define LINK_RX_QUEUE_SIZE 256
static queue_t link_rx_queue;
static volatile uint32_t link_rx_dropped = 0;

static volatile uint32_t link_tx_frames = 0; // frames sent by any task, for link_poll

static uint16_t debug_address = 0;
static uint16_t debug_data = 0;

#ifdef __FREERTOS
static SemaphoreHandle_t spi_bus_mutex = nullptr;
static TaskHandle_t dispatch_task = nullptr;
static TaskHandle_t event_task = nullptr;
#endif

void link_queue_init() {
  queue_init(&spi_event_queue, sizeof(queue_spi_t), SPI_EVENT_QUEUE_SIZE);
  queue_init(&link_rx_queue, sizeof(queue_spi_t), LINK_RX_QUEUE_SIZE);
}

void link_os_init() {
#ifdef __FREERTOS
  spi_bus_mutex = xSemaphoreCreateRecursiveMutex();
#endif
}

void spi_bus_lock() {
#ifdef __FREERTOS
  if (spi_bus_mutex) xSemaphoreTakeRecursive(spi_bus_mutex, portMAX_DELAY);
#endif
}

void spi_bus_unlock() {
#ifdef __FREERTOS
  if (spi_bus_mutex) xSemaphoreGiveRecursive(spi_bus_mutex);
#endif
}

// hooks called by lib/FT812 around its own SPI0 transactions
extern "C" void ft812_bus_lock(void) { spi_bus_lock(); }
extern "C" void ft812_bus_unlock(void) { spi_bus_unlock(); }

void link_set_dispatch_task(void *task) {
#ifdef __FREERTOS
  dispatch_task = (TaskHandle_t) task;
#endif
}

void link_set_event_task(void *task) {
#ifdef __FREERTOS
  event_task = (TaskHandle_t) task;
#endif
}

static inline bool in_dispatch_context() {
#ifdef __FREERTOS
  return dispatch_task == nullptr || xTaskGetCurrentTaskHandle() == dispatch_task;
#else
  return true;
#endif
}

// The fpga can only answer inside a transfer started by the MCU. Called once per main loop
// iteration: if nothing at all was sent since the previous call, send a NOP so the fpga gets
// a chance to deliver its commands (uart, rtc, matrix, flashboot, ...).
void link_poll() {
  static uint32_t seen = 0;
  if (link_tx_frames == seen) {
    spi_send(CMD_NOP, 0, 0);
  }
  seen = link_tx_frames;
}

// handle frames received by other tasks, called by the dispatch task
void link_dispatch_pending() {
  queue_spi_t packet;
  while (queue_try_remove(&link_rx_queue, &packet)) {
    process_in_cmd(packet.cmd, packet.addr, packet.data);
  }
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
  if (!enabled) {
    // frames from the previous core must not reach the next one
    queue_spi_t packet;
    while (queue_try_remove(&link_rx_queue, &packet)) {}
  }
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
    return;
  }
#ifdef __FREERTOS
  // wake the input task (called from core1 tasks, core0 tasks and the ps/2 repeat alarm irq)
  if (event_task) {
    if (portCHECK_IF_IN_ISR()) {
      BaseType_t woken = pdFALSE;
      vTaskNotifyGiveFromISR(event_task, &woken);
      portYIELD_FROM_ISR(woken);
    } else {
      xTaskNotifyGive(event_task);
    }
  }
#endif
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
  static uint32_t prev_link_rx_dropped = 0;
  if (link_rx_dropped != prev_link_rx_dropped) {
    prev_link_rx_dropped = link_rx_dropped;
    d_printf("FPGA rx queue overflow, dropped: %lu", (unsigned long) link_rx_dropped); d_println();
  }
}

static SPISettings link_settings() {
  // default (16 MHz) or custom spi freq from the core config
  return (link_freq == 0 || link_freq == 255) ? settingsA : SPISettings(SD_SCK_MHZ(link_freq), MSBFIRST, SPI_MODE0);
}

// one classic frame: send cmd/addr/data, receive the fpga's frame of the same transfer
static void spi_xfer(uint8_t cmd, uint8_t addr, uint8_t data, uint8_t rx[3]) {
  spi_bus_lock();
  SPI.beginTransaction(link_settings());
  // one 3-byte transfer keeps the SPI FIFO busy instead of 3 separate blocking calls
  const uint8_t tx[3] = {cmd, addr, data};
  gpio_put(PIN_MCU_SPI_CS, LOW);
  SPI.transfer(tx, rx, 3);
  gpio_put(PIN_MCU_SPI_CS, HIGH);
  SPI.endTransaction();
  link_tx_frames++;
  spi_bus_unlock();
}

void spi_send(uint8_t cmd, uint8_t addr, uint8_t data) {
  uint8_t rx[3];
  spi_xfer(cmd, addr, data, rx);
  // handlers run outside the bus lock (unless a caller holds an outer SpiBusGuard), so a
  // handler waiting for another bus (I2C) does not hold up the other spi users
  if ((rx[0] > 0) && link_rx_enabled) {
    if (in_dispatch_context()) {
      process_in_cmd(rx[0], rx[1], rx[2]);
    } else {
      queue_spi_t packet = { rx[0], rx[1], rx[2] };
      if (!queue_try_add(&link_rx_queue, &packet)) link_rx_dropped++;
    }
  }
}

// multi-byte values are sent as consecutive addr 0..n-1, least significant byte first
static void spi_send_le(uint8_t cmd, uint64_t data, uint8_t len) {
  SpiBusGuard bus; // keep the bytes of one value together
  for (uint8_t i=0; i<len; i++) {
    spi_send(cmd, i, (uint8_t)(data >> (8*i)));
  }
}

void spi_send16(uint8_t cmd, uint16_t data) { spi_send_le(cmd, data, 2); }
void spi_send24(uint8_t cmd, uint32_t data) { spi_send_le(cmd, data, 3); }
void spi_send32(uint8_t cmd, uint32_t data) { spi_send_le(cmd, data, 4); }
void spi_send64(uint8_t cmd, uint64_t data) { spi_send_le(cmd, data, 8); }

//--------------------------------------------------------------------------------------------
// Burst transfers (docs/burst_protocol.md): a second chip select, a 5-byte header (type +
// 32-bit address) and up to BURST_MAX_LEN data bytes back to back. Used only when the loaded
// core announced support in its CMD_CAPS answer.

#define CAPS_BURST_WRITE 0x01
#define CAPS_BURST_READY 0x02
#define CAPS_QUERY_POLLS 8
#define BURST_READY_TIMEOUT_US 100000

static uint8_t burst_caps = 0;
static uint8_t burst_mhz = 0;

void link_burst_reset() {
  burst_caps = 0;
  burst_mhz = 0;
  pinMode(PIN_BURST_CS, INPUT); // only driven for cores that support bursts
}

void link_caps_query() {
  SpiBusGuard bus;
  uint8_t rx[3];
  // query (protocol version 1), then poll: the answer comes in the reply bytes of a later transfer
  spi_xfer(CMD_CAPS, 0x00, 0x01, rx);
  for (uint8_t i=0; i<CAPS_QUERY_POLLS && rx[0] != CMD_CAPS; i++) {
    spi_xfer(CMD_NOP, 0, 0, rx);
  }
  bool answered = (rx[0] == CMD_CAPS);

  if (answered) {
    burst_caps = rx[1];
    burst_mhz = rx[2];
    d_printf("FPGA caps: %02x, burst max %u MHz", burst_caps, burst_mhz); d_println();
  } else {
    d_println("FPGA caps: none (classic link)");
  }
  if (burst_caps & CAPS_BURST_WRITE) {
    pinMode(PIN_BURST_CS, OUTPUT);
    digitalWrite(PIN_BURST_CS, HIGH);
  }
}

bool link_burst_available() {
  return (burst_caps & CAPS_BURST_WRITE) != 0;
}

static bool burst_wait_ready() {
  if (!(burst_caps & CAPS_BURST_READY)) return true;
  uint32_t start = time_us_32();
  while (!digitalRead(PIN_BURST_READY)) {
    if (time_us_32() - start >= BURST_READY_TIMEOUT_US) return false;
  }
  return true;
}

size_t link_burst_write(uint8_t type, uint32_t addr, const uint8_t *data, size_t len) {
  if (!link_burst_available()) return 0;
  SpiBusGuard bus;
  // Begin with the classic link settings and switch only the baud rate: a different SPISettings
  // clock makes arduino-pico deinit/init the SPI block, twice per burst when classic frames
  // (OSD progress etc.) come in between.
  SPISettings settings = link_settings();
  SPI.beginTransaction(settings);
  if (burst_mhz) spi_set_baudrate(spi0, burst_mhz * 1000000UL);
  size_t sent = 0;
  while (sent < len) {
    size_t n = len - sent;
    if (n > BURST_MAX_LEN) n = BURST_MAX_LEN;
    if (!burst_wait_ready()) {
      d_println("Burst: fpga not ready, falling back to classic frames");
      break;
    }
    uint32_t a = addr + sent;
    const uint8_t hdr[5] = { type, (uint8_t)(a >> 24), (uint8_t)(a >> 16), (uint8_t)(a >> 8), (uint8_t) a };
    gpio_put(PIN_BURST_CS, LOW);
    SPI.transfer(hdr, nullptr, sizeof(hdr));
    SPI.transfer(data + sent, nullptr, n);
    gpio_put(PIN_BURST_CS, HIGH);
    link_tx_frames++;
    sent += n;
  }
  if (burst_mhz) spi_set_baudrate(spi0, settings.getClockFreq());
  SPI.endTransaction();
  return sent;
}
