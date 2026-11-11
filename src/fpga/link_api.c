#include "main.h"
#include "fpga/link_api.h"
#include "storage/img_mount.h"

// link_api.h on link v1: the frames of docs/protocol.md through the transport of link.c. The frame
// sequences are the ones the services used to send themselves, in the same order.

// ---- capabilities ----

bool link_has(link_cap_t cap) {
  switch (cap) {
    case LINK_CAP_BK: return (link_core_caps() & CAPS_BK) != 0;
    case LINK_CAP_MIDI: return (link_core_caps() & CAPS_MIDI) != 0;
    case LINK_CAP_IMG_WP: return (link_core_caps() & CAPS_IMG_WP) != 0;
    case LINK_CAP_BURST: return link_burst_available();
  }
  return false;
}

const char *link_mode_name(void) {
  return link_burst_available() ? "burst" : "classic";
}

void link_core_unload(void) {
  link_set_rx_enabled(false);
  link_burst_reset(); // the new core may not support bursts
}

bool link_core_hello(void) {
  return link_caps_query();
}

void link_core_ready(void) {
  link_set_rx_enabled(true);
}

void link_core_freq(uint8_t mhz) {
  link_set_freq(mhz);
}

void link_poll_once(void) {
  spi_send(CMD_NOP, 0, 0);
}

// ---- SYS ----

void link_sys_init(bool start) {
  spi_send(start ? CMD_INIT_START : CMD_INIT_DONE, 0, 0);
}

void link_sys_hw_setup(uint8_t key, uint8_t value) {
  spi_send(CMD_HW_SETUP, key, value);
}

void link_sys_build_ids(const char *core_build, const char *fw_build) {
  bool end_core = false, end_fw = false;
  for (uint8_t i=0; i<8; i++) {
    if (!core_build[i]) end_core = true;
    if (!fw_build[i]) end_fw = true;
    spi_send(CMD_HW_SETUP, 0x10 + i, end_core ? 0 : (uint8_t) core_build[i]);
    spi_send(CMD_HW_SETUP, 0x18 + i, end_fw ? 0 : (uint8_t) fw_build[i]);
  }
}

void link_sys_setval(uint8_t index, uint8_t value) {
  spi_send(CMD_SWITCHES, index, value);
}

void link_sys_btn(uint8_t button, bool pressed) {
  spi_send(CMD_BTN, button, pressed);
}

void link_sys_spi_control(uint8_t bits) {
  spi_send(CMD_SPI_CONTROL, 0, bits);
}

static void (*reload_cb)(uint8_t) = NULL;
static void on_flashboot(uint8_t addr, uint8_t data) {
  (void) addr;
  if (reload_cb) reload_cb(data);
}
void link_sys_on_reload(void (*cb)(uint8_t core_id)) {
  reload_cb = cb;
  link_on(CMD_FLASHBOOT, on_flashboot);
}

// ---- HID ----

static const uint8_t event_cmd[] = {
  [LINK_EV_KBD] = CMD_USB_KBD,
  [LINK_EV_MOUSE] = CMD_USB_MOUSE,
  [LINK_EV_PS2] = CMD_PS2_SCANCODE,
  [LINK_EV_MIDI] = CMD_MIDI,
};

void link_hid_queue(link_event_kind_t kind, uint8_t idx, uint8_t val) {
  link_event_push(kind, idx, val);
}

bool link_hid_queue_pop(link_event_t *ev) {
  queue_spi_t p;
  if (!link_queue_pop(&p)) return false;
  ev->kind = p.cmd;
  ev->idx = p.addr;
  ev->val = p.data;
  return true;
}

void link_hid_forward(const link_event_t *ev) {
  if (ev->kind < sizeof(event_cmd)) spi_send(event_cmd[ev->kind], ev->idx, ev->val);
}

void link_hid_joy(uint8_t port, uint16_t bits) {
  spi_send(CMD_JOYSTICK, 2 * port, (uint8_t) bits);
  spi_send(CMD_JOYSTICK, 2 * port + 1, (uint8_t) (bits >> 8));
}

void link_hid_matrix_btn(uint8_t button, bool pressed) {
  spi_send(CMD_MATRIX_BTN, button, pressed);
}

void link_hid_on_ps2_cmd(void (*cb)(uint8_t cmd, uint8_t val)) {
  link_on(CMD_PS2_SCANCODE, cb);
}

// ---- LOAD ----

void link_load_start(link_load_kind_t kind, uint8_t slot, uint64_t size, const char *ext) {
  switch (kind) {
    case LINK_LOAD_ROM:
      spi_send(CMD_ROMLOADER, 0, 1);
      break;
    case LINK_LOAD_FILE:
      spi_send(CMD_FILELOADER, 0, 3); // active = 1, reset = 1
      spi_send(CMD_FILELOADER, 0, 2); // active = 1, reset = 0
      break;
    case LINK_LOAD_IOCTL:
      spi_bus_lock();
      spi_send(CMD_IOCTL_STATE, 0, 1); // start
      spi_send(CMD_IOCTL_SLOT, 0, slot);
      spi_send64(CMD_IOCTL_SIZE, 0);
      spi_send64(CMD_IOCTL_SIZE, size);
      for (uint8_t i = 0; ext && ext[i] && i < 255; i++) {
        spi_send(CMD_IOCTL_EXT, i, ext[i]);
      }
      spi_bus_unlock();
      break;
  }
}

void link_load_data(link_load_kind_t kind, uint32_t addr, const uint8_t *buf, size_t len) {
  switch (kind) {
    case LINK_LOAD_ROM: link_write_data(CMD_ROMDATA, CMD_ROMBANK, addr, buf, len); break;
    case LINK_LOAD_FILE: link_write_data(CMD_FILEDATA, CMD_FILEBANK, addr, buf, len); break;
    case LINK_LOAD_IOCTL: link_write_data(CMD_IOCTL_DATA, CMD_IOCTL_BANK, addr, buf, len); break;
  }
}

void link_load_end(link_load_kind_t kind) {
  switch (kind) {
    case LINK_LOAD_ROM: spi_send(CMD_ROMLOADER, 0, 0); break;
    case LINK_LOAD_FILE: spi_send(CMD_FILELOADER, 0, 0); break; // active = 0, reset = 0
    case LINK_LOAD_IOCTL: spi_send(CMD_IOCTL_STATE, 0, 0); break; // finish
  }
}

// ---- IMG ----
//
// The fpga asks for a sector in its reply frames: IMG_LBA (4 bytes), then IMG_SEC (addr = slot,
// data = read / write). The handlers only take the request; the mounter serves it from the main
// loop, so a sector transfer never lands in the middle of another sequence on the link.
//
// A frame can get lost on the link now and then, so every request is checked end to end and the
// core sends it again when the answer is an error:
// - a request counts only with all 4 IMG_LBA bytes since IMG_LBA 0;
// - read: the 16-bit sum of the sector follows the data (IMG_LBA 0 / 1, mcu -> fpga);
// - write: the sum follows the data (IMG_BUF_BANK 10 / 11); the sector counts only with all
//   512 bytes and the same sum.

static uint32_t req_lba = 0;
static uint8_t req_slot = 0;
static uint8_t req_op = 0;
static uint8_t lba_mask = 0;          // IMG_LBA bytes since IMG_LBA 0
static bool req_complete = false;     // the request had all 4 IMG_LBA bytes
static uint8_t req_parts = 0;         // the IMG_LBA bytes of the request, for the log
static volatile bool req_pending = false;
static volatile bool req_started = false; // IMG_LBA seen, IMG_SEC not yet
static uint32_t req_start_us = 0;

// sector data written by the core: IMG_BUF_BANK (offset bits 8..15) + IMG_BUF_DATA (offset
// bits 0..7, byte), then its sum in IMG_BUF_BANK 10 / 11
static uint8_t wr_buf[LINK_SECTOR_SIZE];
static uint8_t wr_seen[LINK_SECTOR_SIZE / 8]; // bytes received, one bit each
static uint16_t wr_bank = 0;
static uint16_t wr_count = 0;
static uint16_t wr_sum = 0;
static uint8_t wr_sum_seen = 0;       // bit 0: low byte, bit 1: high byte

#define IMG_WRITE_POLLS 4000 // NOPs to pull one sector of write data (519 frames with the request)
#define IMG_REQ_POLLS 32     // NOPs to pull the rest of a request (5 frames)

// a write request: the data follows it at once, and any frame on the link pulls it, also the
// ones of the INPUT task (mouse, keys) before the mounter holds the bus; so start counting here
static void wr_reset() {
  wr_bank = 0;
  wr_count = 0;
  wr_sum_seen = 0;
  memset(wr_seen, 0, sizeof(wr_seen));
}

static uint16_t sector_sum(const uint8_t *buf) {
  uint16_t sum = 0;
  for (int i = 0; i < LINK_SECTOR_SIZE; i++) sum += buf[i];
  return sum;
}

static void on_img_lba(uint8_t addr, uint8_t data) {
  if (addr == 0) {
    req_started = true;
    req_start_us = us_now();
    lba_mask = 0;
  }
  if (addr < 4) {
    req_lba = (req_lba & ~(0xFFUL << (8 * addr))) | ((uint32_t) data << (8 * addr));
    lba_mask |= 1 << addr;
  }
}

static void on_img_sec(uint8_t addr, uint8_t data) {
  req_slot = addr;
  req_op = data;
  if (data == IMG_REQ_WRITE) wr_reset();
  req_complete = (lba_mask == 0x0F);
  req_parts = lba_mask;
  lba_mask = 0;
  req_pending = true;
  req_started = false;
}

static void on_img_buf_bank(uint8_t addr, uint8_t data) {
  if (addr == 0) {
    wr_bank = data;
  } else if (addr == 0x10) {
    wr_sum = (wr_sum & 0xFF00) | data;
    wr_sum_seen |= 1;
  } else if (addr == 0x11) {
    wr_sum = (wr_sum & 0x00FF) | (data << 8);
    wr_sum_seen |= 2;
  }
}

static void on_img_buf_data(uint8_t addr, uint8_t data) {
  uint16_t pos = (wr_bank << 8) | addr;
  if (pos < LINK_SECTOR_SIZE) {
    wr_buf[pos] = data;
    if (!(wr_seen[pos >> 3] & (1 << (pos & 7)))) {
      wr_seen[pos >> 3] |= 1 << (pos & 7);
      wr_count++;
    }
  }
}

void link_img_begin(void) {
  link_on(CMD_IMG_LBA, on_img_lba);
  link_on(CMD_IMG_SEC, on_img_sec);
  link_on(CMD_IMG_BUF_BANK, on_img_buf_bank);
  link_on(CMD_IMG_BUF_DATA, on_img_buf_data);
}

void link_img_mount(uint8_t slot, uint64_t size, bool wp) {
  // size 0 first: the core sees an eject before a new image
  spi_bus_lock();
  spi_send(CMD_IMG_SLOT, 0, slot);
  spi_send64(CMD_IMG_SIZE, 0);
  if (size > 0) {
    spi_send64(CMD_IMG_SIZE, size);
    if (link_has(LINK_CAP_IMG_WP)) spi_send(CMD_IMG_FLAGS, slot, wp ? IMG_FLAG_WP : 0);
  }
  spi_bus_unlock();
}

void link_img_mount_phys(uint8_t slot, uint8_t drive) {
  spi_bus_lock();
  spi_send(CMD_IMG_SLOT, 0, slot);
  spi_send64(CMD_IMG_SIZE, 0);
  spi_send(CMD_IMG_PHYS, slot, drive + 1);
  spi_bus_unlock();
}

void link_img_reset(void) {
  req_pending = false;
  req_started = false;
  lba_mask = 0;
}

bool link_img_waiting(void) {
  return req_pending || req_started;
}

bool link_img_take(link_img_req_t *req) {
  // the fpga answers only in the reply bytes of a transfer: a started request is pulled at once,
  // not one frame per main loop iteration
  for (int i = 0; i < IMG_REQ_POLLS && req_started && !req_pending; i++) {
    spi_send(CMD_NOP, 0, 0);
  }
  if (!req_pending) return false;
  req_pending = false;
  req->slot = req_slot;
  req->op = req_op;
  req->lba = req_lba;
  req->complete = req_complete;
  req->parts = req_parts;
  req->start_us = req_start_us;
  return true;
}

void link_img_expire(uint32_t timeout_us) {
  // a request that started but never got its IMG_SEC: the core sends it again
  if (req_started && us_now() - req_start_us > timeout_us) {
    req_started = false;
  }
}

void link_img_send_sector(const uint8_t *buf) {
  // a burst if the core supports it (~0.2 ms a sector), classic bank + data frames for the rest
  // (~5 ms a sector, a real floppy ~18 ms). The core checks the sum either way.
  spi_bus_lock();
  size_t done = link_burst_write(CMD_IMG_BUF_DATA, 0, buf, LINK_SECTOR_SIZE);
  for (size_t i = done; i < LINK_SECTOR_SIZE; i++) {
    if (i == done || (i & 0xFF) == 0) {
      spi_send(CMD_IMG_BUF_BANK, 0, i >> 8);
    }
    spi_send(CMD_IMG_BUF_DATA, i & 0xFF, buf[i]);
  }
  // the sum, so the core can check what it got
  uint16_t sum = sector_sum(buf);
  spi_send(CMD_IMG_LBA, 0, sum & 0xFF);
  spi_send(CMD_IMG_LBA, 1, sum >> 8);
  spi_bus_unlock();
}

bool link_img_receive_sector(uint8_t *buf) {
  // the core queues its write data and the sum behind the request: pull the rest with NOPs
  // (wr_reset() ran on the request, some of it may have come already)
  for (int i = 0; i < IMG_WRITE_POLLS && (wr_count < LINK_SECTOR_SIZE || wr_sum_seen != 3); i++) {
    spi_send(CMD_NOP, 0, 0);
  }
  bool ok = wr_count == LINK_SECTOR_SIZE && wr_sum_seen == 3 && wr_sum == sector_sum(wr_buf);
  memcpy(buf, wr_buf, LINK_SECTOR_SIZE);
  return ok;
}

void link_img_done(uint8_t slot, uint8_t res) {
  spi_send(CMD_IMG_SEC, slot, res);
}

void link_img_flush(uint8_t slot) {
  spi_send(CMD_IMG_FLUSH, slot, 0);
}

static void (*flush_cb)(uint8_t, uint8_t) = NULL;
static void on_img_flush(uint8_t addr, uint8_t data) {
  if (flush_cb) flush_cb(addr, data);
}
void link_img_on_flush(void (*cb)(uint8_t slot, uint8_t result)) {
  flush_cb = cb;
  link_on(CMD_IMG_FLUSH, on_img_flush);
}

// CMD_BK_MOUNT from the fpga: 48 00..03 size in sectors (LE), 48 10 slot, 48 80 kind (the last
// one asks: 0 = .sav, n = save state n)
static uint32_t bk_size = 0;
static uint8_t bk_slot = 0;
static void (*bk_cb)(uint32_t, uint8_t, uint8_t) = NULL;
static void on_bk_mount(uint8_t addr, uint8_t data) {
  if (addr < 4) {
    bk_size = (bk_size & ~(0xFFUL << (8 * addr))) | ((uint32_t) data << (8 * addr));
  } else if (addr == 0x10) {
    bk_slot = data;
  } else if (addr == 0x80) {
    if (bk_cb) bk_cb(bk_size, bk_slot, data);
  }
}
void link_img_on_bk_mount(void (*cb)(uint32_t sectors, uint8_t slot, uint8_t kind)) {
  bk_cb = cb;
  link_on(CMD_BK_MOUNT, on_bk_mount);
}

// DSKCHG request of the core (CMD_FDD_CHNG 49 d 00): it holds the drive selected until the
// answer, so the level is the one of that drive now
static int (*fdd_chng_cb)(uint8_t) = NULL;
static void on_fdd_chng(uint8_t addr, uint8_t data) {
  if (data != 0 || !fdd_chng_cb) return;
  int v = fdd_chng_cb(addr);
  spi_send(CMD_FDD_CHNG, addr, v < 0 ? 2 : (uint8_t) v);
}
void link_img_on_fdd_chng(int (*cb)(uint8_t drive)) {
  fdd_chng_cb = cb;
  link_on(CMD_FDD_CHNG, on_fdd_chng);
}

// ---- PORT ----

void link_uart_put(uint8_t byte) {
  // addr changes with every byte, so the core can see a new one
  static uint8_t uart_idx = 0;
  uart_idx++;
  spi_send(CMD_UART, uart_idx, byte);
}

void link_esp_put(uint8_t byte) {
  spi_send(CMD_ESP_UART, 0, byte);
}

void link_printer_status(uint8_t status) {
  spi_send(CMD_PRINTER_STATUS, 0, status);
}

void link_uart_on_rx(void (*cb)(uint8_t kind, uint8_t byte)) {
  link_on(CMD_UART, cb);
}

static void (*esp_cb)(uint8_t) = NULL;
static void on_esp_uart(uint8_t addr, uint8_t data) {
  (void) addr;
  if (esp_cb) esp_cb(data);
}
void link_esp_on_rx(void (*cb)(uint8_t byte)) {
  esp_cb = cb;
  link_on(CMD_ESP_UART, on_esp_uart);
}

static void (*printer_data_cb)(uint8_t) = NULL;
static void (*printer_req_cb)(void) = NULL;
static void on_printer_data(uint8_t addr, uint8_t data) {
  if (addr == 0 && printer_data_cb) printer_data_cb(data); // nonzero addresses are ignored
}
static void on_printer_status(uint8_t addr, uint8_t data) {
  (void) data;
  if (addr == 0 && printer_req_cb) printer_req_cb();
}
void link_printer_on_data(void (*cb)(uint8_t byte)) {
  printer_data_cb = cb;
  link_on(CMD_PRINTER_DATA, on_printer_data);
}
void link_printer_on_status_request(void (*cb)(void)) {
  printer_req_cb = cb;
  link_on(CMD_PRINTER_STATUS, on_printer_status);
}

// ---- RTC ----

void link_rtc_set(uint8_t reg, uint8_t value) {
  spi_send(CMD_RTC, reg, value);
}

void link_rtc_on_write(void (*cb)(uint8_t reg, uint8_t value)) {
  link_on(CMD_RTC, cb);
}

// ---- AUX ----

static const uint8_t aux_cmd[LINK_AUX_COUNT] = {
  [LINK_AUX_MATRIX_CTL] = CMD_MATRIX_CTL,
  [LINK_AUX_MATRIX_PIXEL] = CMD_MATRIX_PIXEL,
  [LINK_AUX_MATRIX_BITPLAN] = CMD_MATRIX_BITPLAN,
  [LINK_AUX_PEAK_L] = CMD_AUDIO_PEAKS_L,
  [LINK_AUX_PEAK_R] = CMD_AUDIO_PEAKS_R,
};

void link_aux_on(link_aux_kind_t kind, void (*cb)(uint8_t a, uint8_t b)) {
  if (kind < LINK_AUX_COUNT) link_on(aux_cmd[kind], cb);
}

// ---- OSD ----

void link_osd_reg(uint8_t reg, uint8_t value) {
  spi_send(CMD_OSD, reg, value);
}
