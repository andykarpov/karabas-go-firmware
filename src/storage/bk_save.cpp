#include "main.h"
#include "bk_save.h"
#include "file_name.h"

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
static char game_dir[256] = "";
static char game_name[256] = "";

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
  snprintf(game_dir, sizeof(game_dir), "%s", dir);
  snprintf(game_name, sizeof(game_name), "%s", name);
}

// cuts the '/' at the end of dir, down to keep chars
static void strip_slashes(char *dir, size_t keep) {
  size_t len = strlen(dir);
  while (len > keep && dir[len - 1] == '/') dir[--len] = '\0';
}

// the save dir of the core: its header field, or the dir of its games + /saves
static void bk_dir(char *out, size_t size) {
  char dir[KG_PATH_MAX];
  snprintf(dir, sizeof(dir), "%s", core.save_dir);
  kg_trim(dir);
  if (!dir[0]) {
    snprintf(dir, sizeof(dir), "%s", game_dir);
    kg_trim(dir);
    if (!dir[0]) snprintf(dir, sizeof(dir), "%s", core.dir);
    kg_trim(dir);
    strip_slashes(dir, 0);
    strlcat(dir, "/saves", sizeof(dir));
  }
  snprintf(out, size, "%s%s", dir[0] == '/' ? "" : "/", dir);
  strip_slashes(out, 1);
}

// the file of a request: <dir>/<game name without its extension>.sav (or .ss<kind>)
static void bk_file_name(uint8_t kind, char *out, size_t size) {
  char base[sizeof(game_name)];
  strcpy(base, game_name);
  char *dot = strrchr(base, '.');
  if (dot && dot > base) *dot = '\0';
  if (!base[0]) strcpy(base, "game");
  if (kind == 0) snprintf(out, size, "%s.sav", base);
  else snprintf(out, size, "%s.ss%u", base, kind);
}

// open or create the file with at least size bytes (new bytes 0): the core writes any sector of it
static bool bk_make_file(const char *dir, const char *name, uint32_t size) {
  if (!sd1.exists(dir) && !sd1.mkdir(dir, true)) {
    d_printf("Save: unable to make dir %s", dir); d_println();
    return false;
  }
  char path[KG_PATH_MAX];
  kg_path_join(path, sizeof(path), dir, name);
  FsFile f;
  if (!f.open(&sd1, path, O_RDWR | O_CREAT)) {
    d_printf("Save: unable to open %s", path); d_println();
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
  d_printf("Save: %s, %lu bytes%s", path, (unsigned long) size, ok ? "" : ", FAILED to extend"); d_println();
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
  if (size > 0 && size <= BK_MAX_SECTORS && game_name[0]) {
    char dir[sizeof(fs.dir)], name[sizeof(fs.filename)];
    bk_dir(dir, sizeof(dir));
    bk_file_name(req_kind, name, sizeof(name));
    if (bk_make_file(dir, name, size * IMG_SECTOR_SIZE)) {
      strlcpy(fs.dir, dir, sizeof(fs.dir));
      strlcpy(fs.filename, name, sizeof(fs.filename));
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
  game_name[0] = '\0';
}
