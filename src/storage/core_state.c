#include "main.h"
#include "storage/core_state.h"
#include "ini_read.h"
#include "ini_update.h"
#include "file_name.h"
#include "crc32.h"

#define STATE_INI_MAX 16384
#define STATE_SAVE_QUIET_MS 2000
#define STATE_RETRY_MS 30000

// the option keys of the core, in a pool: opt_key[item][option] is an offset, NO_KEY none
#define KEY_POOL_SIZE 2048
#define NO_KEY 0xFFFF
static char key_pool[KEY_POOL_SIZE];
static uint16_t key_pool_len = 0;
static uint16_t opt_key[MAX_OSD_ITEMS][MAX_OSD_ITEM_OPTIONS];

// what the file holds: the crc of the value of each slot, the switches in prev_val
static uint32_t slot_saved[MAX_FILE_SLOTS];
static char loader_path[KG_PATH_MAX];
static bool loader_dirty = false;
static bool dirty = false;
static uint32_t changed_at = 0;
static bool failed = false;

static bool is_kgo2(void) { return core.format == CORE_FORMAT_KGO2; }

// /config/<id><ext>: the id with the chars a file name cannot have as '_'
static void state_path(char *out, size_t size, const char *ext) {
  char id[sizeof(core.id)];
  snprintf(id, sizeof(id), "%s", core.id);
  kg_trim(id);
  for (char *c = id; *c; c++) {
    if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_')) *c = '_';
  }
  snprintf(out, size, CORE_STATE_DIR "/%s%s", id[0] ? id : "core", ext);
}

void core_state_begin(void) {
  key_pool_len = 0;
  memset(opt_key, 0xFF, sizeof(opt_key));
  loader_path[0] = '\0';
  loader_dirty = dirty = failed = false;
}

void core_state_opt_key(uint8_t pos, uint8_t j, const char *key) {
  if (pos >= MAX_OSD_ITEMS || j >= MAX_OSD_ITEM_OPTIONS || !key[0]) return;
  size_t n = strlen(key) + 1;
  if (key_pool_len + n > KEY_POOL_SIZE) {
    d_printf("Core state: no room for the key %s", key); d_println();
    return;
  }
  memcpy(key_pool + key_pool_len, key, n);
  opt_key[pos][j] = key_pool_len;
  key_pool_len += n;
}

static const char *option_key(uint8_t pos, uint8_t j) {
  return opt_key[pos][j] == NO_KEY ? NULL : key_pool + opt_key[pos][j];
}

// the items whose value is kept: S with options, F / FL; with a key
static bool kept_switch(uint8_t i) {
  return core.osd[i].type == CORE_OSD_TYPE_SWITCH && core.osd[i].options_len > 0 && core.osd[i].key[0];
}
static bool kept_file(uint8_t i) {
  return core_osd_is_file(i) && core.osd[i].key[0];
}

// the value of a slot as the ini keeps it: "" none, ":phys0" a drive, else its full path
static void slot_value(uint8_t slot, char *out, size_t size) {
  const core_file_slot_t *fs = &file_slots[slot];
  if (!fs->is_mounted || !fs->filename[0]) out[0] = '\0';
  else if (kg_phys_drive(fs->filename) >= 0) snprintf(out, size, "%s", fs->filename);
  else {
    kg_path_join(out, size, fs->dir, fs->filename);
    kg_path_collapse(out);
  }
}

static uint32_t slot_crc(uint8_t slot) {
  char v[KG_PATH_MAX];
  slot_value(slot, v, sizeof(v));
  return kg_crc32_final(kg_crc32_update(KG_CRC32_INIT, (const uint8_t *) v, strlen(v)));
}

// a file value of the ini into item i: "" ejected, a drive, a path that exists; a file that is
// gone leaves the slot empty
static void apply_file(uint8_t i, const char *v) {
  core_file_slot_t *fs = &file_slots[core.osd[i].slot_id];
  fs->filename[0] = '\0';
  fs->is_mounted = false;
  if (!v[0]) return;
  if (kg_phys_drive(v) >= 0) {
    if (core_osd_phys(i, v)) {
      snprintf(fs->filename, sizeof(fs->filename), "%s", v);
      fs->is_mounted = true;
    }
    return;
  }
  if (!sd_exists(v)) {
    d_printf("Core state: %s is gone, slot %u empty", v, core.osd[i].slot_id); d_println();
    return;
  }
  const char *slash = strrchr(v, '/');
  snprintf(fs->filename, sizeof(fs->filename), "%s", slash ? slash + 1 : v);
  if (slash) {
    size_t n = (size_t) (slash - v);
    if (n >= sizeof(fs->dir)) n = sizeof(fs->dir) - 1;
    memcpy(fs->dir, v, n);
    fs->dir[n] = '\0';
  }
  kg_path_root(fs->dir, sizeof(fs->dir));
  fs->is_mounted = true;
}

void core_state_load(void) {
  if (!is_kgo2() || !has_sd) return;
  char path[64], tmp[64];
  state_path(path, sizeof(path), ".ini");
  state_path(tmp, sizeof(tmp), ".new");
  if (!sd_exists(path) && sd_exists(tmp)) f_rename(tmp, path); // a save cut off between the two

  size_t len = 0;
  char *text = sd_read_all(path, STATE_INI_MAX, &len);
  if (text && !kg_ini_valid(text, len, KG_INI_LONG_LINE_MAX + 2)) {
    d_printf("Core state: %s is invalid, the defaults are used", path); d_println();
    free(text);
    text = NULL;
  }
  if (text) {
    char v[KG_PATH_MAX];
    uint8_t n = 0;
    for (uint8_t i = 0; i < core.osd_len; i++) {
      core_osd_t *o = &core.osd[i];
      if (kept_switch(i) && kg_ini_get(text, len, "osd", o->key, v, sizeof(v))) {
        for (uint8_t j = 0; j < o->options_len; j++) {
          const char *k = option_key(i, j);
          if (k && strcmp(k, v) == 0) { o->val = o->prev_val = j; n++; break; }
        }
      } else if (kept_file(i) && kg_ini_get(text, len, "file", o->key, v, sizeof(v))) {
        apply_file(i, v);
        n++;
      }
    }
    if (kg_ini_get(text, len, "loader", "file", v, sizeof(v)) && v[0]) {
      if (sd_exists(v)) {
        snprintf(core.loader_file, sizeof(core.loader_file), "%s", v);
        n++;
      } else {
        d_printf("Core state: the loader file %s is gone", v); d_println();
      }
    }
    free(text);
    d_printf("Core state: %u values from %s", n, path); d_println();
  }
  for (uint8_t s = 0; s < MAX_FILE_SLOTS; s++) slot_saved[s] = slot_crc(s);

  // the eeprom of banks 4..254
  if (core.eeprom_bank >= MAX_EEPROM_BANKS && core.eeprom_bank != NO_EEPROM_BANK) {
    state_path(path, sizeof(path), ".eep");
    FIL *f = sd_tmp_open(path, FA_READ);
    if (f) {
      uint8_t e[MAX_EEPROM_ITEMS];
      if (sd_read(f, e, sizeof(e)) == sizeof(e)) {
        for (uint16_t i = 0; i < MAX_EEPROM_ITEMS; i++) core.eeprom[i].val = core.eeprom[i].prev_val = e[i];
        d_printf("Core state: eeprom from %s", path); d_println();
      }
      sd_tmp_close();
    }
  }
}

void core_state_changed(void) {
  if (!is_kgo2()) return;
  dirty = true;
  failed = false;
  changed_at = ms_now();
}

void core_state_loader_file(const char *path) {
  if (!is_kgo2()) return;
  snprintf(loader_path, sizeof(loader_path), "%s", path);
  loader_dirty = true;
  core_state_changed();
}

bool core_state_eep_save(const uint8_t *data) {
  if (!has_sd || msc_active() || !sd_mkdirs(CORE_STATE_DIR)) return false;
  char path[64];
  state_path(path, sizeof(path), ".eep");
  FIL *f = sd_tmp_open(path, FA_WRITE | FA_CREATE_ALWAYS);
  if (!f) return false;
  bool ok = sd_write(f, data, MAX_EEPROM_ITEMS) == MAX_EEPROM_ITEMS;
  if (!sd_tmp_close()) ok = false;
  d_printf("Core state: eeprom %s %s", ok ? "saved to" : "not saved to", path); d_println();
  return ok;
}

bool core_state_flush(void) {
  if (!is_kgo2() || !dirty) return true;
  if (!has_sd || msc_active()) return false;

  static kg_ini_item_t items[MAX_OSD_ITEMS + 2];
  uint8_t count = 0;
  // the switches that changed: their option key, or no line for the default
  for (uint8_t i = 0; i < core.osd_len; i++) {
    core_osd_t *o = &core.osd[i];
    if (!kept_switch(i) || o->val == o->prev_val) continue;
    items[count++] = (kg_ini_item_t) { "osd", o->key, o->val == o->def ? NULL : option_key(i, o->val), false };
  }
  // the images that changed (one value per slot)
  char (*paths)[KG_PATH_MAX] = NULL;
  uint32_t crc[MAX_FILE_SLOTS];
  bool slot_done[MAX_FILE_SLOTS] = { false };
  for (uint8_t i = 0; i < core.osd_len; i++) {
    if (!kept_file(i)) continue;
    uint8_t s = core.osd[i].slot_id;
    if (slot_done[s]) continue;
    slot_done[s] = true;
    crc[s] = slot_crc(s);
    if (crc[s] == slot_saved[s]) continue;
    if (!paths && !(paths = malloc(sizeof(*paths) * MAX_FILE_SLOTS))) return false;
    slot_value(s, paths[s], sizeof(paths[s]));
    items[count++] = (kg_ini_item_t) { "file", core.osd[i].key, paths[s], false };
  }
  if (loader_dirty) items[count++] = (kg_ini_item_t) { "loader", "file", loader_path, false };

  bool ok = true;
  if (count > 0) {
    items[count++] = (kg_ini_item_t) { "core", "build", core.build, false };
    char path[64], tmp[64];
    state_path(path, sizeof(path), ".ini");
    state_path(tmp, sizeof(tmp), ".new");
    ok = sd_mkdirs(CORE_STATE_DIR) && ini_file_update(path, tmp, items, count, KG_INI_LONG_LINE_MAX);
  }
  free(paths);
  if (!ok) {
    failed = true;
    changed_at = ms_now();
    return false;
  }
  for (uint8_t i = 0; i < core.osd_len; i++) core.osd[i].prev_val = core.osd[i].val;
  for (uint8_t s = 0; s < MAX_FILE_SLOTS; s++) if (slot_done[s]) slot_saved[s] = crc[s];
  loader_dirty = dirty = failed = false;
  return true;
}

void core_state_tick(void) {
  if (!dirty) return;
  if (ms_since(changed_at) < (failed ? STATE_RETRY_MS : STATE_SAVE_QUIET_MS)) return;
  core_state_flush();
}

bool core_state_reset(void) {
  if (!has_sd || msc_active()) return false;
  bool ok = true;
  const char *exts[] = { ".ini", ".new", ".eep" };
  for (uint8_t k = 0; k < 3; k++) {
    char path[64];
    state_path(path, sizeof(path), exts[k]);
    if (sd_exists(path) && f_unlink(path) != FR_OK) ok = false;
  }
  dirty = loader_dirty = failed = false;
  d_printf("Core state: reset %s", ok ? "done" : "failed"); d_println();
  return ok;
}
