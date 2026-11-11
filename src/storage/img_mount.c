#include "main.h"
#include "img_mount.h"
#include "file_name.h"

// Sector requests of the core come through link_api (link_img_take); img_tick() serves them from
// the main loop, so a sector transfer never lands in the middle of another sequence on the link.
// The link checks a request end to end (docs/protocol.md section 6).

static uint8_t img_buf[IMG_SECTOR_SIZE];

// written sectors are flushed to the sd card once the writes stop (a sync per sector takes ~40 ms)
static bool img_dirty[MAX_FILE_SLOTS];
static uint8_t wp_logged = 0; // the slots whose dropped writes are logged, one bit each
static uint32_t img_last_write_ms = 0;
#define IMG_SYNC_IDLE_MS 500

// the log: a failed request at once, the others summed up (a line per sector flooded the debug
// buffer, and the boot log or a usb disconnect line with it)
#define IMG_LOG_IDLE_MS 1000   // a summary once the requests stop for this long
#define IMG_LOG_EVERY_MS 10000 // or this often while they go on
static uint32_t log_rd = 0, log_wr = 0;
static uint32_t writes = 0; // all written sectors, img_writes()
static uint32_t log_max_wait_us = 0, log_max_serve_us = 0;
static uint32_t log_first_ms = 0, log_last_ms = 0;
static uint8_t log_slots = 0; // slots in the summary, one bit each

#define IMG_FOLLOW_US 2000   // after a request, poll for the next one (the next sector of a track)
#define IMG_TICK_MAX_US 20000 // at most this long in one img_tick()

// DSKCHG request of the core: it holds the drive selected until the answer, so the level is the
// one of that drive now
static int fdd_chng(uint8_t drive) {
  (void) drive;
  return board_fdd_chng();
}

void img_register_handlers() {
  link_img_begin();
  link_img_on_fdd_chng(fdd_chng);
}

void img_close_all() {
  for (uint8_t i = 0; i < MAX_FILE_SLOTS; i++) {
    sd_close(&file_slots[i].file); // f_close syncs
    img_dirty[i] = false;
    matrix_drive_mount(i, 0);
  }
  link_img_reset();
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
    link_img_mount_phys(slot, phys);
    return;
  }
  fs->wp = false;
  wp_logged &= ~(1u << slot);
  if (fs->is_mounted) {
    char fname[KG_PATH_MAX];
    img_path(slot, fname, sizeof(fname));
    // read-write, so the core can write sectors back; read only if the file is write protected,
    // or the item is read-only (kgo2 "readonly")
    bool rw = !fs->readonly && sd_open(&fs->file, fname, FA_READ | FA_WRITE);
    if (!rw && !sd_open(&fs->file, fname, FA_READ)) {
      d_printf("Mount slot %d: unable to open %s", slot, fname); d_println();
      fs->is_mounted = false;
    } else {
      fs->wp = !rw;
      fsize = f_size(&fs->file);
      d_printf("Mount slot %d: %s, %llu bytes%s", slot, fname, (unsigned long long) fsize,
        fs->wp ? ", write protected" : ""); d_println();
    }
  }
  matrix_drive_mount(slot, (uint32_t) ((fsize + IMG_SECTOR_SIZE - 1) / IMG_SECTOR_SIZE));
  link_img_mount(slot, fsize, fs->wp); // an eject first, then the image
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

// one request; false if none came
static bool img_serve() {
  link_img_req_t req;
  if (!link_img_take(&req)) return false;
  uint32_t serve_us = us_now();
  uint8_t slot = req.slot;
  uint32_t lba = req.lba;
  uint8_t req_op = req.op;
  bool req_complete = req.complete;
  bool ok = false;
  uint8_t res = 0; // the answer, if not done / error
  bool dropped = false; // a write to a write protected image
  const char *what = req_op == IMG_REQ_WRITE ? "wr" : "rd";
  spi_bus_lock(); // the sector and its result stay together on the link
  if (!req_complete) {
    what = "incomplete request"; // a lost IMG_LBA byte: the core asks again
  } else if (slot < MAX_FILE_SLOTS) {
    FIL *f = &file_slots[slot].file;
    if (req_op == IMG_REQ_READ) {
      ok = img_read_sector(slot, lba);
      link_img_send_sector(img_buf);
    } else if (req_op == IMG_REQ_WRITE) {
      if (!link_img_receive_sector(img_buf)) {
        what = "wr, data lost";
      } else if (file_slots[slot].wp) {
        // the data is pulled (the link stays in step) and dropped: a core that knows write
        // protection gets "protected", an older one "done", so it does not ask again forever
        dropped = true;
        res = link_has(LINK_CAP_IMG_WP) ? IMG_RES_PROTECTED : IMG_RES_DONE;
        if (!(wp_logged & (1u << slot))) {
          wp_logged |= 1u << slot;
          d_printf("Image slot %d: write protected, writes dropped", slot); d_println();
        }
      } else {
        ok = img_seek(slot, lba) && sd_write(f, img_buf, IMG_SECTOR_SIZE) == IMG_SECTOR_SIZE;
        img_dirty[slot] = true;
        img_last_write_ms = ms_now();
      }
    }
  }
  if (res == 0) res = ok ? IMG_RES_DONE : IMG_RES_ERROR;
  else ok = true; // a write protected write: answered, logged once per mount
  link_img_done(slot, res);
  if (req_complete) matrix_drive_access(slot, lba, req_op == IMG_REQ_WRITE);
  uint32_t end_us = us_now();
  uint32_t wait_us = serve_us - req.start_us, took_us = end_us - serve_us;
  if (!ok) {
    d_printf("Image slot %d: %s %lu FAILED, wait %lu us, %lu us", slot, what, (unsigned long) lba,
      (unsigned long) wait_us, (unsigned long) took_us);
    d_println();
  } else {
    if (log_rd + log_wr == 0) log_first_ms = ms_now();
    if (req_op == IMG_REQ_WRITE) { if (!dropped) { log_wr++; writes++; } } else log_rd++;
    if (slot < 8) log_slots |= 1 << slot;
    if (wait_us > log_max_wait_us) log_max_wait_us = wait_us;
    if (took_us > log_max_serve_us) log_max_serve_us = took_us;
  }
  log_last_ms = ms_now();
  spi_bus_unlock();
  return true;
}

uint32_t img_writes() {
  return writes;
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
  if (!link_img_waiting()) {
    img_sync_idle();
    return;
  }
  if (!img_serve()) {
    link_img_expire(100000); // a request that never ended: the core sends it again
    return;
  }
  // a track is read sector by sector: keep polling for the next request for a while, but give
  // the main loop its turn now and then
  uint32_t t = us_now();
  uint32_t start = t;
  while (us_now() - t < IMG_FOLLOW_US && us_now() - start < IMG_TICK_MAX_US) {
    if (link_img_waiting()) {
      if (img_serve()) t = us_now();
    } else {
      link_poll_once();
    }
  }
}
