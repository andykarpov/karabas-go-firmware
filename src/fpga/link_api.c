#include "main.h"
#include "fpga/link_api.h"
#include "storage/img_mount.h"

// link_api.h on link v1: the frames of docs/protocol.md through the transport of link.c. The frame
// sequences are the ones the services used to send themselves, in the same order.

// ---- link v2 (docs/link_v2.md): packets ----

#define V2 (link_version() == 2)
#define T_SYS 0x00
#define T_HID 0x01
#define T_LOAD 0x04

// caps of STATUS (docs/link_v2.md 5.1)
#define V2_CAP_BK 0x0002
#define V2_CAP_IMG_WP 0x0004
#define V2_CAP_LOAD_READY 0x0010
#define V2_CAP_MIDI 0x0200
static uint16_t v2_caps = 0;

static void v2_send(const uint8_t *p, size_t n) {
  link2_xfer(p, n, NULL, 0);
}

// a command without parameters that answers: T C Z, then n bytes
static void v2_read(uint8_t t, uint8_t c, uint8_t *rx, size_t n) {
  const uint8_t tx[3] = { t, c, 0 };
  link2_xfer(tx, sizeof(tx), rx, n);
}

// ---- capabilities ----

bool link_has(link_cap_t cap) {
  if (V2) {
    switch (cap) {
      case LINK_CAP_BK: return (v2_caps & V2_CAP_BK) != 0;
      case LINK_CAP_MIDI: return (v2_caps & V2_CAP_MIDI) != 0;
      case LINK_CAP_IMG_WP: return (v2_caps & V2_CAP_IMG_WP) != 0;
      case LINK_CAP_BURST: return true; // every packet is one
    }
    return false;
  }
  switch (cap) {
    case LINK_CAP_BK: return (link_core_caps() & CAPS_BK) != 0;
    case LINK_CAP_MIDI: return (link_core_caps() & CAPS_MIDI) != 0;
    case LINK_CAP_IMG_WP: return (link_core_caps() & CAPS_IMG_WP) != 0;
    case LINK_CAP_BURST: return link_burst_available();
  }
  return false;
}

const char *link_mode_name(void) {
  if (V2) return "v2";
  return link_burst_available() ? "burst" : "classic";
}

void link_core_unload(void) {
  link_set_rx_enabled(false);
  link_burst_reset(); // the new core may not support bursts
}

#define V2_HELLO_MS 100

bool link_core_hello(void) {
  if (!V2) return link_caps_query();
  // STATUS until the core answers 'K' 'G' 2 (its link may still be in reset after CONF_DONE)
  uint8_t st[8];
  uint32_t t = ms_now();
  do {
    v2_read(T_SYS, 0x00, st, sizeof(st));
    if (st[0] == 'K' && st[1] == 'G' && st[2] == 2) {
      v2_caps = st[3] | (st[4] << 8);
      uint8_t causes;
      v2_read(T_SYS, 0x02, &causes, 1); // clears the coldboot cause
      d_printf("Link v2: core answered in %lu ms, caps %04x, osd %ux%u, ports %02x", (unsigned long) ms_since(t),
        v2_caps, st[5] * 8, st[6] * 8, st[7]); d_println();
      return true;
    }
  } while (ms_since(t) < V2_HELLO_MS);
  v2_caps = 0;
  d_printf("Link v2: no answer of the core (%02x %02x %02x)", st[0], st[1], st[2]); d_println();
  return false;
}

void link_core_ready(void) {
  link_set_rx_enabled(true); // v1: frames of the core again (v2: events come on INT)
}

void link_core_freq(uint8_t mhz) {
  link_set_freq(mhz);
}

void link_poll_once(void) {
  if (V2) { link_v2_service(); return; }
  spi_send(CMD_NOP, 0, 0);
}

// ---- SYS ----

void link_sys_init(bool start) {
  if (V2) { const uint8_t p[3] = { T_SYS, 0x05, start ? 1 : 0 }; v2_send(p, 3); return; }
  spi_send(start ? CMD_INIT_START : CMD_INIT_DONE, 0, 0);
}

void link_sys_hw_setup(uint8_t key, uint8_t value) {
  if (V2) { const uint8_t p[4] = { T_SYS, 0x04, key, value }; v2_send(p, 4); return; }
  spi_send(CMD_HW_SETUP, key, value);
}

void link_sys_build_ids(const char *core_build, const char *fw_build) {
  bool end_core = false, end_fw = false;
  if (V2) {
    // one packet: key 10, the 8 characters of the core, the 8 of the firmware
    uint8_t p[3 + 16] = { T_SYS, 0x04, 0x10 };
    for (uint8_t i=0; i<8; i++) {
      if (!core_build[i]) end_core = true;
      if (!fw_build[i]) end_fw = true;
      p[3 + i] = end_core ? 0 : (uint8_t) core_build[i];
      p[11 + i] = end_fw ? 0 : (uint8_t) fw_build[i];
    }
    v2_send(p, sizeof(p));
    return;
  }
  for (uint8_t i=0; i<8; i++) {
    if (!core_build[i]) end_core = true;
    if (!fw_build[i]) end_fw = true;
    spi_send(CMD_HW_SETUP, 0x10 + i, end_core ? 0 : (uint8_t) core_build[i]);
    spi_send(CMD_HW_SETUP, 0x18 + i, end_fw ? 0 : (uint8_t) fw_build[i]);
  }
}

void link_sys_setval(uint8_t index, uint8_t value) {
  if (V2) { const uint8_t p[4] = { T_SYS, 0x03, index, value }; v2_send(p, 4); return; }
  spi_send(CMD_SWITCHES, index, value);
}

void link_sys_btn(uint8_t button, bool pressed) {
  if (V2) { const uint8_t p[4] = { T_SYS, 0x07, button, pressed }; v2_send(p, 4); return; }
  spi_send(CMD_BTN, button, pressed);
}

void link_sys_spi_control(uint8_t bits) {
  if (V2) { const uint8_t p[3] = { T_SYS, 0x06, bits }; v2_send(p, 3); return; }
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

// v2: a whole report per packet, from the events of the queue (KBD 0..6, MOUSE 0..3)
static uint8_t v2_kbd[8], v2_mouse[4];

static void v2_forward(const link_event_t *ev) {
  switch (ev->kind) {
    case LINK_EV_KBD:
      if (ev->idx > 6) return;
      v2_kbd[ev->idx ? ev->idx + 1 : 0] = ev->val; // byte 1 of the report is reserved
      if (ev->idx == 6) {
        uint8_t p[2 + 8] = { T_HID, 0x01 };
        memcpy(p + 2, v2_kbd, 8);
        v2_send(p, sizeof(p));
      }
      break;
    case LINK_EV_MOUSE:
      if (ev->idx > 3) return;
      v2_mouse[ev->idx] = ev->val;
      // the buttons end a report; a lone wheel 0 after a report (hid_app) needs no packet
      if (ev->idx == 3) {
        uint8_t p[2 + 4] = { T_HID, 0x03 };
        memcpy(p + 2, v2_mouse, 4);
        v2_send(p, sizeof(p));
      }
      break;
    case LINK_EV_PS2: {
      const uint8_t p[4] = { T_HID, 0x02, ev->idx, ev->val };
      v2_send(p, 4);
      break;
    }
    default: break; // MIDI: PORT 2 comes with Ф5
  }
}

void link_hid_forward(const link_event_t *ev) {
  if (V2) { v2_forward(ev); return; }
  if (ev->kind < sizeof(event_cmd)) spi_send(event_cmd[ev->kind], ev->idx, ev->val);
}

void link_hid_joy(uint8_t port, uint16_t bits) {
  if (V2) { const uint8_t p[5] = { T_HID, 0x04, port, (uint8_t) bits, (uint8_t) (bits >> 8) }; v2_send(p, 5); return; }
  spi_send(CMD_JOYSTICK, 2 * port, (uint8_t) bits);
  spi_send(CMD_JOYSTICK, 2 * port + 1, (uint8_t) (bits >> 8));
}

void link_hid_matrix_btn(uint8_t button, bool pressed) {
  if (V2) { link_sys_btn(2 + button, pressed); return; }
  spi_send(CMD_MATRIX_BTN, button, pressed);
}

static void (*ps2_cmd_cb)(uint8_t, uint8_t) = NULL;
void link_hid_on_ps2_cmd(void (*cb)(uint8_t cmd, uint8_t val)) {
  ps2_cmd_cb = cb;
  link_on(CMD_PS2_SCANCODE, cb);
}

// ---- LOAD ----

void link_load_start(link_load_kind_t kind, uint8_t slot, uint64_t size, const char *ext) {
  if (V2) {
    uint8_t p[2 + 10 + 8] = { T_LOAD, 0x01, (uint8_t) kind, slot };
    for (int i = 0; i < 8; i++) p[4 + i] = (uint8_t) (size >> (8 * i));
    size_t n = 12;
    for (uint8_t i = 0; ext && ext[i] && i < 8; i++) p[n++] = (uint8_t) ext[i];
    v2_send(p, n);
    return;
  }
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

#define V2_LOAD_CHUNK 512
#define V2_READY_MS 100

// LOAD STATUS until the core can take a packet (caps bit 4); false after V2_READY_MS
static bool v2_load_ready(void) {
  if (!(v2_caps & V2_CAP_LOAD_READY)) return true;
  uint32_t t = ms_now();
  do {
    uint8_t r;
    v2_read(T_LOAD, 0x04, &r, 1);
    if (r) return true;
    ms_delay(1);
  } while (ms_since(t) < V2_READY_MS);
  d_println("Link v2: the core is not ready for load data");
  return false;
}

static void v2_load_data(link_load_kind_t kind, uint32_t addr, const uint8_t *buf, size_t len) {
  static uint8_t p[2 + 5 + V2_LOAD_CHUNK];
  spi_bus_lock();
  for (size_t done = 0; done < len; ) {
    size_t n = len - done < V2_LOAD_CHUNK ? len - done : V2_LOAD_CHUNK;
    if (!v2_load_ready()) break;
    uint32_t a = addr + done;
    p[0] = T_LOAD; p[1] = 0x02; p[2] = (uint8_t) kind;
    p[3] = (uint8_t) a; p[4] = (uint8_t) (a >> 8); p[5] = (uint8_t) (a >> 16); p[6] = (uint8_t) (a >> 24);
    memcpy(p + 7, buf + done, n);
    v2_send(p, 7 + n);
    done += n;
  }
  spi_bus_unlock();
}

void link_load_data(link_load_kind_t kind, uint32_t addr, const uint8_t *buf, size_t len) {
  if (V2) { v2_load_data(kind, addr, buf, len); return; }
  switch (kind) {
    case LINK_LOAD_ROM: link_write_data(CMD_ROMDATA, CMD_ROMBANK, addr, buf, len); break;
    case LINK_LOAD_FILE: link_write_data(CMD_FILEDATA, CMD_FILEBANK, addr, buf, len); break;
    case LINK_LOAD_IOCTL: link_write_data(CMD_IOCTL_DATA, CMD_IOCTL_BANK, addr, buf, len); break;
  }
}

void link_load_end(link_load_kind_t kind) {
  if (V2) { const uint8_t p[3] = { T_LOAD, 0x03, (uint8_t) kind }; v2_send(p, 3); return; }
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

// ---- link v2: the events of the core ----

void link_v2_service(void) {
  if (!V2 || !link2_int()) return;
  uint8_t pending;
  {
    const uint8_t tx[4] = { T_SYS, 0x01, 0x00, 0x00 }; // IRQ: no ack, Z, pending
    link2_xfer(tx, sizeof(tx), &pending, 1);
  }
  if (pending & 0x01) {
    uint8_t causes;
    v2_read(T_SYS, 0x02, &causes, 1);
    if (causes & 0x01) { d_println("Link v2: the link of the core restarted"); }
    if (causes & 0x02) {
      uint8_t core_id;
      v2_read(T_SYS, 0x08, &core_id, 1);
      if (reload_cb) reload_cb(core_id);
    }
  }
  if (pending & 0x02) {
    // HID EVENT: the count, then (command, argument) pairs; at most 8 (the fifo of the core)
    uint8_t ev[1 + 16];
    v2_read(T_HID, 0x06, ev, sizeof(ev));
    for (uint8_t i = 0; i < ev[0] && i < 8; i++) {
      if (ps2_cmd_cb) ps2_cmd_cb(ev[1 + 2 * i], ev[2 + 2 * i]);
    }
  }
}
