#include "main.h"
#include "bk_save.h"

// CMD_BK_MOUNT from the fpga: 48 00..03 size in sectors (LE), 48 10 slot, 48 80 kind (the last
// one asks: 0 = .sav, n = save state n). The handler only takes it; bk_tick() makes the file, so
// the sd card is used from the main loop only.
static uint32_t req_size = 0;
static uint8_t req_slot = 0;
static uint8_t req_kind = 0;
static volatile bool req_pending = false;

// CMD_IMG_FLUSH answer: 47 slot 01 done / 02 error
static volatile uint8_t flush_result = 0;

static int8_t bk_slot = -1;      // the img slot of the mounted save
static String game_dir = "";
static String game_name = "";

#define BK_FLUSH_TIMEOUT_MS 3000 // 64 sectors: ~5 ms each on the link, plus the sd card
#define BK_MAX_SECTORS 2048      // 1 MB: a sane limit for a request

void bk_register_handlers() {
  link_on(CMD_BK_MOUNT, [](uint8_t addr, uint8_t data) {
    if (addr < 4) {
      req_size = (req_size & ~(0xFFUL << (8 * addr))) | ((uint32_t) data << (8 * addr));
    } else if (addr == 0x10) {
      req_slot = data;
    } else if (addr == 0x80) {
      req_kind = data;
      req_pending = true;
    }
  });
  link_on(CMD_IMG_FLUSH, [](uint8_t addr, uint8_t data) {
    if (bk_slot >= 0 && addr == (uint8_t) bk_slot) flush_result = data;
  });
}

void bk_set_game(const char *dir, const char *name) {
  game_dir = String(dir);
  game_name = String(name);
}

// the save dir of the core: its header field, or the dir of its games + /saves
static String bk_dir() {
  String dir = String(core.save_dir);
  dir.trim();
  if (dir.length() == 0) {
    dir = game_dir;
    dir.trim();
    if (dir.length() == 0) dir = String(core.dir);
    dir.trim();
    while (dir.endsWith("/")) dir.remove(dir.length() - 1);
    dir += "/saves";
  }
  if (dir.charAt(0) != '/') dir = "/" + dir;
  while (dir.length() > 1 && dir.endsWith("/")) dir.remove(dir.length() - 1);
  return dir;
}

// the file of a request: <dir>/<game name without its extension>.sav (or .ss<kind>)
static String bk_file_name(uint8_t kind) {
  String base = game_name;
  int dot = base.lastIndexOf('.');
  if (dot > 0) base = base.substring(0, dot);
  if (base.length() == 0) base = "game";
  return base + (kind == 0 ? String(".sav") : String(".ss") + String(kind));
}

// open or create the file with at least size bytes (new bytes 0): the core writes any sector of it
static bool bk_make_file(const String &dir, const String &name, uint32_t size) {
  if (!sd1.exists(dir.c_str()) && !sd1.mkdir(dir.c_str(), true)) {
    d_printf("Save: unable to make dir %s", dir.c_str()); d_println();
    return false;
  }
  String path = dir + "/" + name;
  FsFile f;
  if (!f.open(&sd1, path.c_str(), O_RDWR | O_CREAT)) {
    d_printf("Save: unable to open %s", path.c_str()); d_println();
    return false;
  }
  uint64_t have = f.fileSize();
  bool ok = true;
  if (have < size) {
    static uint8_t zeros[IMG_SECTOR_SIZE];
    memset(zeros, 0, sizeof(zeros));
    ok = f.seekSet(have);
    while (ok && have < size) {
      uint32_t n = size - have < IMG_SECTOR_SIZE ? size - have : IMG_SECTOR_SIZE;
      ok = f.write(zeros, n) == n;
      have += n;
    }
  }
  f.close();
  d_printf("Save: %s, %lu bytes%s", path.c_str(), (unsigned long) size, ok ? "" : ", FAILED to extend"); d_println();
  return ok;
}

void bk_tick() {
  if (!req_pending) return;
  req_pending = false;
  uint8_t slot = req_slot;
  uint32_t size = req_size;
  if (slot >= MAX_FILE_SLOTS) return;
  if (bk_slot >= 0 && bk_slot != slot) bk_close();
  core_file_slot_t &fs = file_slots[slot];
  fs.is_mounted = false;
  if (size > 0 && size <= BK_MAX_SECTORS && game_name.length() > 0) {
    String dir = bk_dir();
    String name = bk_file_name(req_kind);
    if (bk_make_file(dir, name, size * IMG_SECTOR_SIZE)) {
      strlcpy(fs.dir, dir.c_str(), sizeof(fs.dir));
      strlcpy(fs.filename, name.c_str(), sizeof(fs.filename));
      fs.is_mounted = true;
    }
  }
  // the size of the file, or 0: the core goes on without a save
  img_mount(slot);
  bk_slot = fs.is_mounted ? slot : -1;
}

bool bk_flush() {
  if (bk_slot < 0 || !(link_core_caps() & CAPS_BK)) return true;
  flush_result = 0;
  spi_send(CMD_IMG_FLUSH, bk_slot, 0);
  ElapsedTimer timer;
  timer.reset();
  // the core writes its sectors through the file mounter: serve them while waiting for the answer
  while (flush_result == 0 && timer.elapsed() < BK_FLUSH_TIMEOUT_MS) {
    img_tick();
    spi_send(CMD_NOP, 0, 0);
  }
  bool ok = flush_result == IMG_RES_DONE;
  d_printf("Save: flush slot %d %s in %lu ms", bk_slot, flush_result == 0 ? "no answer" : ok ? "done" : "FAILED",
    (unsigned long) timer.elapsed()); d_println();
  if (file_slots[bk_slot].file.isOpen()) file_slots[bk_slot].file.sync();
  return ok;
}

void bk_close() {
  if (bk_slot < 0) return;
  file_slots[bk_slot].is_mounted = false;
  img_mount(bk_slot); // closes the file, size 0 to the core
  bk_slot = -1;
}

void bk_forget() {
  bk_slot = -1;
  req_pending = false;
  game_name = "";
}
