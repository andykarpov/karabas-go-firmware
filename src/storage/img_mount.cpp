#include "main.h"
#include "img_mount.h"

// The fpga asks for a sector in its reply frames: IMG_LBA (4 bytes), then IMG_SEC (addr = slot,
// data = read / write). The handlers only take the request; img_tick() serves it from the main
// loop, so a sector transfer never lands in the middle of another sequence on the link.

static uint32_t req_lba = 0;
static uint8_t req_slot = 0;
static uint8_t req_op = 0;
static volatile bool req_pending = false;

// sector data written by the core: IMG_BUF_BANK (offset bits 8..15) + IMG_BUF_DATA (offset
// bits 0..7, byte), as for the roms
static uint8_t img_buf[IMG_SECTOR_SIZE];
static uint16_t wr_bank = 0;
static uint16_t wr_count = 0;

#define IMG_WRITE_POLLS 4000 // NOPs to pull one sector of write data (517 frames with the request)

void img_register_handlers() {
  link_on(CMD_IMG_LBA, [](uint8_t addr, uint8_t data) {
    if (addr < 4) {
      req_lba = (req_lba & ~(0xFFUL << (8 * addr))) | ((uint32_t) data << (8 * addr));
    }
  });
  link_on(CMD_IMG_SEC, [](uint8_t addr, uint8_t data) {
    req_slot = addr;
    req_op = data;
    req_pending = true;
  });
  link_on(CMD_IMG_BUF_BANK, [](uint8_t addr, uint8_t data) {
    wr_bank = data;
  });
  link_on(CMD_IMG_BUF_DATA, [](uint8_t addr, uint8_t data) {
    uint16_t pos = (wr_bank << 8) | addr;
    if (pos < IMG_SECTOR_SIZE) {
      img_buf[pos] = data;
      wr_count++;
    }
  });
}

void img_close_all() {
  for (uint8_t i = 0; i < MAX_FILE_SLOTS; i++) {
    if (file_slots[i].file.isOpen()) {
      file_slots[i].file.close();
    }
  }
  req_pending = false;
}

void img_mount(uint8_t slot) {
  if (slot >= MAX_FILE_SLOTS) return;
  core_file_slot_t &fs = file_slots[slot];
  if (fs.file.isOpen()) {
    fs.file.close();
  }
  uint64_t fsize = 0;
  if (fs.is_mounted) {
    String dir = String(fs.dir);
    if (dir == "") { dir = "/"; }
    if (dir.charAt(0) != '/') { dir = '/' + dir; }
    String fname = dir + "/" + String(fs.filename);
    // read-write, so the core can write sectors back; read only if the file is write protected
    if (!fs.file.open(&sd1, fname.c_str(), O_RDWR) && !fs.file.open(&sd1, fname.c_str(), O_RDONLY)) {
      d_printf("Mount slot %d: unable to open %s", slot, fname.c_str()); d_println();
      fs.is_mounted = false;
    } else {
      fsize = fs.file.fileSize();
      d_printf("Mount slot %d: %s, %lu bytes", slot, fname.c_str(), (unsigned long) fsize); d_println();
    }
  }
  // size 0 first: the core sees an eject before a new image
  SpiBusGuard bus;
  spi_send(CMD_IMG_SLOT, 0, slot);
  spi_send64(CMD_IMG_SIZE, 0);
  if (fsize > 0) {
    spi_send64(CMD_IMG_SIZE, fsize);
  }
}

static bool img_read_sector(File32 &f, uint32_t lba) {
  memset(img_buf, 0, IMG_SECTOR_SIZE);
  if (!f.isOpen() || !f.seekSet((uint64_t) lba * IMG_SECTOR_SIZE)) return false;
  // a short last sector is padded with zeros
  return f.read(img_buf, IMG_SECTOR_SIZE) > 0;
}

static void img_send_sector() {
  // bursts if the core supports them; classic bank + data frames for the rest
  size_t done = link_burst_write(CMD_IMG_BUF_DATA, 0, img_buf, IMG_SECTOR_SIZE);
  for (size_t i = done; i < IMG_SECTOR_SIZE; i++) {
    if (i == done || (i & 0xFF) == 0) {
      spi_send(CMD_IMG_BUF_BANK, 0, i >> 8);
    }
    spi_send(CMD_IMG_BUF_DATA, i & 0xFF, img_buf[i]);
  }
}

static bool img_receive_sector() {
  // the core queues its write data behind the request: pull it with NOPs
  wr_bank = 0;
  wr_count = 0;
  for (int i = 0; i < IMG_WRITE_POLLS && wr_count < IMG_SECTOR_SIZE; i++) {
    spi_send(CMD_NOP, 0, 0);
  }
  return wr_count >= IMG_SECTOR_SIZE;
}

void img_tick() {
  if (!req_pending) return;
  req_pending = false;
  uint8_t slot = req_slot;
  uint32_t lba = req_lba;
  bool ok = false;
  SpiBusGuard bus; // the sector and its result stay together on the link
  if (slot < MAX_FILE_SLOTS) {
    File32 &f = file_slots[slot].file;
    if (req_op == IMG_REQ_READ) {
      ok = img_read_sector(f, lba);
      img_send_sector();
    } else if (req_op == IMG_REQ_WRITE) {
      ok = img_receive_sector();
      if (ok) {
        ok = f.isOpen() && f.seekSet((uint64_t) lba * IMG_SECTOR_SIZE) &&
             f.write(img_buf, IMG_SECTOR_SIZE) == IMG_SECTOR_SIZE;
        f.sync();
      }
    }
  }
  spi_send(CMD_IMG_SEC, slot, ok ? IMG_RES_DONE : IMG_RES_ERROR);
  if (!ok) {
    d_printf("Image slot %d: %s of sector %lu failed", slot, req_op == IMG_REQ_WRITE ? "write" : "read",
      (unsigned long) lba);
    d_println();
  }
}
