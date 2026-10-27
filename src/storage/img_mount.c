#include "main.h"
#include "img_mount.h"
#include "file_name.h"

// The fpga asks for a sector in its reply frames: IMG_LBA (4 bytes), then IMG_SEC (addr = slot,
// data = read / write). The handlers only take the request; img_tick() serves it from the main
// loop, so a sector transfer never lands in the middle of another sequence on the link.
//
// A frame can get lost on the link now and then, so every request is checked end to end and the
// core sends it again when the answer is an error:
// - a request counts only with all 4 IMG_LBA bytes since IMG_LBA 0;
// - read: the 16-bit sum of the sector follows the data (IMG_LBA 0 / 1, mcu -> fpga);
// - write: the sum follows the data (IMG_BUF_BANK 10 / 11); the sector is written only with all
//   512 bytes and the same sum.

static uint32_t req_lba = 0;
static uint8_t req_slot = 0;
static uint8_t req_op = 0;
static uint8_t lba_mask = 0;          // IMG_LBA bytes since IMG_LBA 0
static bool req_complete = false;     // the request had all 4 IMG_LBA bytes
static volatile bool req_pending = false;
static volatile bool req_started = false; // IMG_LBA seen, IMG_SEC not yet
static uint32_t req_start_us = 0;

// sector data written by the core: IMG_BUF_BANK (offset bits 8..15) + IMG_BUF_DATA (offset
// bits 0..7, byte), then its sum in IMG_BUF_BANK 10 / 11
static uint8_t img_buf[IMG_SECTOR_SIZE];
static uint8_t wr_seen[IMG_SECTOR_SIZE / 8]; // bytes received, one bit each
static uint16_t wr_bank = 0;
static uint16_t wr_count = 0;
static uint16_t wr_sum = 0;
static uint8_t wr_sum_seen = 0;       // bit 0: low byte, bit 1: high byte

// written sectors are flushed to the sd card once the writes stop (a sync per sector takes ~40 ms)
static bool img_dirty[MAX_FILE_SLOTS];
static uint32_t img_last_write_ms = 0;
#define IMG_SYNC_IDLE_MS 500

// the log: a failed request at once, the others summed up (a line per sector flooded the debug
// buffer, and the boot log or a usb disconnect line with it)
#define IMG_LOG_IDLE_MS 1000   // a summary once the requests stop for this long
#define IMG_LOG_EVERY_MS 10000 // or this often while they go on
static uint32_t log_rd = 0, log_wr = 0;
static uint32_t log_max_wait_us = 0, log_max_serve_us = 0;
static uint32_t log_first_ms = 0, log_last_ms = 0;
static uint8_t log_slots = 0; // slots in the summary, one bit each

#define IMG_WRITE_POLLS 4000 // NOPs to pull one sector of write data (519 frames with the request)
#define IMG_REQ_POLLS 32     // NOPs to pull the rest of a request (5 frames)
#define IMG_FOLLOW_US 2000   // after a request, poll for the next one (the next sector of a track)
#define IMG_TICK_MAX_US 20000 // at most this long in one img_tick()

// a write request: the data follows it at once, and any frame on the link pulls it, also the
// ones of the INPUT task (mouse, keys) before img_serve() holds the bus; so start counting here
static void wr_reset() {
  wr_bank = 0;
  wr_count = 0;
  wr_sum_seen = 0;
  memset(wr_seen, 0, sizeof(wr_seen));
}

static uint16_t img_sum(const uint8_t *buf) {
  uint16_t sum = 0;
  for (int i = 0; i < IMG_SECTOR_SIZE; i++) sum += buf[i];
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
  if (pos < IMG_SECTOR_SIZE) {
    img_buf[pos] = data;
    if (!(wr_seen[pos >> 3] & (1 << (pos & 7)))) {
      wr_seen[pos >> 3] |= 1 << (pos & 7);
      wr_count++;
    }
  }
}

void img_register_handlers() {
  link_on(CMD_IMG_LBA, on_img_lba);
  link_on(CMD_IMG_SEC, on_img_sec);
  link_on(CMD_IMG_BUF_BANK, on_img_buf_bank);
  link_on(CMD_IMG_BUF_DATA, on_img_buf_data);
}

void img_close_all() {
  for (uint8_t i = 0; i < MAX_FILE_SLOTS; i++) {
    sd_close(&file_slots[i].file); // f_close syncs
    img_dirty[i] = false;
    matrix_drive_mount(i, 0);
  }
  req_pending = false;
  req_started = false;
  lba_mask = 0;
}

// the path of the image of a slot
static void img_path(uint8_t slot, char *out, size_t size) {
  core_file_slot_t *fs = &file_slots[slot];
  char dir[sizeof(fs->dir) + 1];
  snprintf(dir, sizeof(dir), "%s%s", fs->dir[0] == '/' ? "" : "/", fs->dir);
  kg_path_join(out, size, dir, fs->filename);
}

void img_mount(uint8_t slot) {
  if (slot >= MAX_FILE_SLOTS) return;
  core_file_slot_t *fs = &file_slots[slot];
  sd_close(&fs->file); // f_close syncs
  img_dirty[slot] = false;
  matrix_drive_mount(slot, 0);
  uint64_t fsize = 0;
  int phys = fs->is_mounted ? kg_phys_drive(fs->filename) : -1;
  if (phys >= 0) {
    // a physical floppy drive: the core gets no image, then the drive
    d_printf("Mount slot %d: physical drive DS%d", slot, phys); d_println();
    spi_bus_lock();
    spi_send(CMD_IMG_SLOT, 0, slot);
    spi_send64(CMD_IMG_SIZE, 0);
    spi_send(CMD_IMG_PHYS, slot, phys + 1);
    spi_bus_unlock();
    return;
  }
  if (fs->is_mounted) {
    char fname[KG_PATH_MAX];
    img_path(slot, fname, sizeof(fname));
    // read-write, so the core can write sectors back; read only if the file is write protected
    if (!sd_open(&fs->file, fname, FA_READ | FA_WRITE) && !sd_open(&fs->file, fname, FA_READ)) {
      d_printf("Mount slot %d: unable to open %s", slot, fname); d_println();
      fs->is_mounted = false;
    } else {
      fsize = f_size(&fs->file);
      d_printf("Mount slot %d: %s, %llu bytes", slot, fname, (unsigned long long) fsize); d_println();
    }
  }
  matrix_drive_mount(slot, (uint32_t) ((fsize + IMG_SECTOR_SIZE - 1) / IMG_SECTOR_SIZE));
  // size 0 first: the core sees an eject before a new image
  spi_bus_lock();
  spi_send(CMD_IMG_SLOT, 0, slot);
  spi_send64(CMD_IMG_SIZE, 0);
  if (fsize > 0) {
    spi_send64(CMD_IMG_SIZE, fsize);
  }
  spi_bus_unlock();
}

// FatFs marks a FIL after a disk error (fp->err) and fails everything on it from then on:
// open it again, as it was (read-write, else read only)
static void img_reopen(uint8_t slot) {
  FIL *f = &file_slots[slot].file;
  if (!sd_is_open(f) || !f->err) return;
  char fname[KG_PATH_MAX];
  img_path(slot, fname, sizeof(fname));
  bool rw = f->flag & FA_WRITE;
  f->obj.fs = NULL; // f_close of a FIL with an error would try to sync it
  bool ok = sd_open(f, fname, rw ? FA_READ | FA_WRITE : FA_READ);
  d_printf("Image slot %u: open again after an error: %s", slot, ok ? "ok" : "FAILED"); d_println();
}

static uint8_t img_diag_left = 16; // the failures logged in detail

static void img_fail_log(uint8_t slot, uint32_t lba, const char *what, int res, UINT n) {
  if (!img_diag_left) return;
  img_diag_left--;
  FIL *f = &file_slots[slot].file;
  sd_card_t *c = sd1_card();
  d_printf("Image slot %u: lba %lu: %s %d (%u bytes), size %llu, FIL err %u, card err %u data %02X",
    slot, (unsigned long) lba, what, res, (unsigned) n, (unsigned long long) f_size(f), f->err,
    sd_card_error(c), sd_card_error_data(c)); d_println();
}

// the file position of a sector of the image; false past its end (f_lseek would grow a file
// open for writing, SdFat's seekSet failed there)
static bool img_seek(uint8_t slot, uint32_t lba) {
  FIL *f = &file_slots[slot].file;
  img_reopen(slot);
  FSIZE_t pos = (FSIZE_t) lba * IMG_SECTOR_SIZE;
  if (!sd_is_open(f) || pos >= f_size(f)) return false;
  FRESULT res = f_lseek(f, pos);
  if (res != FR_OK) img_fail_log(slot, lba, "lseek", res, 0);
  return res == FR_OK;
}

static bool img_read_sector(uint8_t slot, uint32_t lba) {
  FIL *f = &file_slots[slot].file;
  memset(img_buf, 0, IMG_SECTOR_SIZE);
  if (!img_seek(slot, lba)) return false;
  // a short last sector is padded with zeros
  UINT n = 0;
  FRESULT res = f_read(f, img_buf, IMG_SECTOR_SIZE, &n);
  if (res != FR_OK || n == 0) img_fail_log(slot, lba, "read", res, n);
  return res == FR_OK && n > 0;
}

static void img_send_sector() {
  // a burst if the core supports it (~0.2 ms a sector), classic bank + data frames for the rest
  // (~5 ms a sector, a real floppy ~18 ms). The core checks the sum either way.
  size_t done = link_burst_write(CMD_IMG_BUF_DATA, 0, img_buf, IMG_SECTOR_SIZE);
  for (size_t i = done; i < IMG_SECTOR_SIZE; i++) {
    if (i == done || (i & 0xFF) == 0) {
      spi_send(CMD_IMG_BUF_BANK, 0, i >> 8);
    }
    spi_send(CMD_IMG_BUF_DATA, i & 0xFF, img_buf[i]);
  }
  // the sum, so the core can check what it got
  uint16_t sum = img_sum(img_buf);
  spi_send(CMD_IMG_LBA, 0, sum & 0xFF);
  spi_send(CMD_IMG_LBA, 1, sum >> 8);
}

static bool img_receive_sector() {
  // the core queues its write data and the sum behind the request: pull the rest with NOPs
  // (wr_reset() ran on the request, some of it may have come already)
  for (int i = 0; i < IMG_WRITE_POLLS && (wr_count < IMG_SECTOR_SIZE || wr_sum_seen != 3); i++) {
    spi_send(CMD_NOP, 0, 0);
  }
  return wr_count == IMG_SECTOR_SIZE && wr_sum_seen == 3 && wr_sum == img_sum(img_buf);
}

// one request; false if none came
static bool img_serve() {
  // the fpga answers only in the reply bytes of a transfer: a started request is pulled at once,
  // not one frame per main loop iteration
  for (int i = 0; i < IMG_REQ_POLLS && req_started && !req_pending; i++) {
    spi_send(CMD_NOP, 0, 0);
  }
  if (!req_pending) return false;
  req_pending = false;
  uint32_t serve_us = us_now();
  uint8_t slot = req_slot;
  uint32_t lba = req_lba;
  bool ok = false;
  const char *what = req_op == IMG_REQ_WRITE ? "wr" : "rd";
  spi_bus_lock(); // the sector and its result stay together on the link
  if (!req_complete) {
    what = "incomplete request"; // a lost IMG_LBA byte: the core asks again
  } else if (slot < MAX_FILE_SLOTS) {
    FIL *f = &file_slots[slot].file;
    if (req_op == IMG_REQ_READ) {
      ok = img_read_sector(slot, lba);
      img_send_sector();
    } else if (req_op == IMG_REQ_WRITE) {
      if (!img_receive_sector()) {
        what = "wr, data lost";
      } else {
        ok = img_seek(slot, lba) && sd_write(f, img_buf, IMG_SECTOR_SIZE) == IMG_SECTOR_SIZE;
        img_dirty[slot] = true;
        img_last_write_ms = ms_now();
      }
    }
  }
  spi_send(CMD_IMG_SEC, slot, ok ? IMG_RES_DONE : IMG_RES_ERROR);
  if (req_complete) matrix_drive_access(slot, lba, req_op == IMG_REQ_WRITE);
  uint32_t end_us = us_now();
  uint32_t wait_us = serve_us - req_start_us, took_us = end_us - serve_us;
  if (!ok) {
    d_printf("Image slot %d: %s %lu FAILED, wait %lu us, %lu us", slot, what, (unsigned long) lba,
      (unsigned long) wait_us, (unsigned long) took_us);
    d_println();
  } else {
    if (log_rd + log_wr == 0) log_first_ms = ms_now();
    if (req_op == IMG_REQ_WRITE) log_wr++; else log_rd++;
    if (slot < 8) log_slots |= 1 << slot;
    if (wait_us > log_max_wait_us) log_max_wait_us = wait_us;
    if (took_us > log_max_serve_us) log_max_serve_us = took_us;
  }
  log_last_ms = ms_now();
  spi_bus_unlock();
  return true;
}

static void img_log_summary() {
  if (log_rd + log_wr == 0) return;
  uint32_t now = ms_now();
  if (now - log_last_ms < IMG_LOG_IDLE_MS && now - log_first_ms < IMG_LOG_EVERY_MS) return;
  d_printf("Image slots %02x: %lu rd, %lu wr in %lu ms, max wait %lu us, max %lu us", log_slots,
    (unsigned long) log_rd, (unsigned long) log_wr, (unsigned long) (log_last_ms - log_first_ms),
    (unsigned long) log_max_wait_us, (unsigned long) log_max_serve_us);
  d_println();
  log_rd = log_wr = 0;
  log_max_wait_us = log_max_serve_us = 0;
  log_slots = 0;
}

static void img_sync_idle() {
  if (ms_now() - img_last_write_ms < IMG_SYNC_IDLE_MS) return;
  for (uint8_t i = 0; i < MAX_FILE_SLOTS; i++) {
    if (img_dirty[i]) {
      img_dirty[i] = false;
      if (sd_is_open(&file_slots[i].file)) {
        f_sync(&file_slots[i].file);
      }
    }
  }
}

void img_tick() {
  img_log_summary();
  if (!req_pending && !req_started) {
    img_sync_idle();
    return;
  }
  if (!img_serve()) {
    // a request that started but never got its IMG_SEC: the core sends it again
    if (req_started && us_now() - req_start_us > 100000) {
      req_started = false;
    }
    return;
  }
  // a track is read sector by sector: keep polling for the next request for a while, but give
  // the main loop its turn now and then
  uint32_t t = us_now();
  uint32_t start = t;
  while (us_now() - t < IMG_FOLLOW_US && us_now() - start < IMG_TICK_MAX_US) {
    if (req_started || req_pending) {
      if (img_serve()) t = us_now();
    } else {
      spi_send(CMD_NOP, 0, 0);
    }
  }
}
