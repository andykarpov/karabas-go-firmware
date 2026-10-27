#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "hid_driver.h"
#include "hid_learn.h"
#include "input/sega.h"
#include "ini_read.h"
#include "hardware/sync.h"

// Loaded by APP (at boot and after the OSD mapper saved a driver), read by the USB task on
// core1. A table is filled before it is published through one pointer (with a barrier) and is
// never changed afterwards. An old table is not freed: core1 may still point into it. That
// leaks a few hundred bytes per save, which is rare.
typedef struct {
  hid_joy_config_t cfg; // first: hid_driver_find() hands out &cfg
  char file[32];
} driver_t;

typedef struct {
  uint8_t len;
  driver_t *list;
} driver_table_t;

static driver_table_t *volatile drivers = nullptr;
volatile uint32_t hid_drivers_gen = 0;

const char *const hid_joy_keys[HID_JOY_BUTTONS] = {"up", "down", "left", "right", "a", "b", "c", "x", "y", "z", "start", "mode"};
const uint16_t hid_joy_btns[HID_JOY_BUTTONS] = {SC_BTN_UP, SC_BTN_DOWN, SC_BTN_LEFT, SC_BTN_RIGHT, SC_BTN_A, SC_BTN_B, SC_BTN_C, SC_BTN_X, SC_BTN_Y, SC_BTN_Z, SC_BTN_START, SC_BTN_MODE};

// a *.ini file whose name fits the driver table (32 bytes)
static bool is_driver_file(const FILINFO *fi)
{
  size_t len = strlen(fi->fname);
  return !(fi->fattrib & AM_DIR) && len > 4 && len < 32 && strcasecmp(fi->fname + len - 4, ".ini") == 0;
}

static hid_joy_config_t hid_driver_load(const char *filename)
{
  hid_joy_config_t cfg = {0};
  cfg.report_id = -1;
  const size_t bufferLen = 80;
  char buffer[bufferLen];
  char fullname[64];
  snprintf(fullname, sizeof(fullname), "/drivers/%s", filename);
  size_t len;
  char *text = sd_read_all(fullname, 8192, &len);
  if (text) {
    // validate file
    if (!kg_ini_valid(text, len, bufferLen)) {
      d_printf("%s is invalid", fullname); d_println();
      free(text);
      return cfg;
    }

    cfg.vid = (kg_ini_get(text, len, "info", "vid", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : 0;
    cfg.pid = (kg_ini_get(text, len, "info", "pid", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : 0;
    if (kg_ini_get(text, len, "info", "report_id", buffer, bufferLen)) cfg.report_id = (uint8_t) strtoul(buffer, 0, 10);

    for(uint8_t i=0; i<HID_JOY_BUTTONS; i++) {
      if (kg_ini_get(text, len, "buttons", hid_joy_keys[i], buffer, bufferLen)) {
        if (!hid_joy_rules_parse(buffer, hid_joy_btns[i], &cfg)) {
          d_printf("%s: invalid mapping for %s: %s", filename, hid_joy_keys[i], buffer); d_println();
        }
      }
    }

    free(text);
  }
  return cfg;
}

void hid_drivers_load()
{
  if (!has_sd) return;
  static DIR dir;
  if (!sd_opendir(&dir, "/drivers")) return;

  FILINFO fi;
  uint16_t count = 0;
  while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
    if (is_driver_file(&fi)) count++;
  }
  if (count > MAX_JOY_DRIVERS) count = MAX_JOY_DRIVERS;

  driver_table_t *t = new driver_table_t;
  t->len = 0;
  t->list = count ? new driver_t[count] : nullptr;
  f_readdir(&dir, nullptr); // rewind
  while (t->len < count && f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
    if (!is_driver_file(&fi)) continue;
    const char *filename = fi.fname;
    hid_joy_config_t cfg = hid_driver_load(filename);
    if (cfg.vid || cfg.pid) {
      t->list[t->len].cfg = cfg;
      strlcpy(t->list[t->len].file, filename, sizeof(t->list[t->len].file));
      t->len++;
    }
  }
  sd_closedir(&dir);

  __dmb();
  drivers = t;
  hid_drivers_gen = hid_drivers_gen + 1;
}

const hid_joy_config_t *hid_driver_find(uint16_t vid, uint16_t pid)
{
  const driver_table_t *t = drivers;
  __dmb();
  if (!t) return nullptr;
  for (uint8_t i = 0; i < t->len; i++) {
    if (t->list[i].cfg.vid == vid && t->list[i].cfg.pid == pid) {
      return &t->list[i].cfg;
    }
  }
  return nullptr;
}

const char *hid_driver_file(const hid_joy_config_t *cfg)
{
  return reinterpret_cast<const driver_t *>(cfg)->file;
}

// the driver files of a pad other than keep become *.ini.bak; the count of them
static uint8_t backup_drivers(uint16_t vid, uint16_t pid, const char *keep)
{
  char others[4][32];
  uint8_t n_others = 0;
  static DIR dir;
  if (sd_opendir(&dir, "/drivers")) {
    FILINFO fi;
    while (n_others < 4 && f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
      if (!is_driver_file(&fi) || (keep && strcasecmp(fi.fname, keep) == 0)) continue;
      hid_joy_config_t old = hid_driver_load(fi.fname);
      if (old.vid == vid && old.pid == pid) strcpy(others[n_others++], fi.fname);
    }
    sd_closedir(&dir);
  }
  for (uint8_t i = 0; i < n_others; i++) {
    char from[64], to[64];
    snprintf(from, sizeof(from), "/drivers/%s", others[i]);
    snprintf(to, sizeof(to), "/drivers/%s.bak", others[i]);
    if (sd_exists(to)) f_unlink(to);
    d_printf("Driver %s renamed to %s", from, to); d_println();
    f_rename(from, to);
  }
  return n_others;
}

bool hid_driver_forget(uint16_t vid, uint16_t pid)
{
  if (!has_sd || msc_active()) return false;
  bool any = backup_drivers(vid, pid, nullptr) > 0;
  hid_drivers_load();
  return any;
}

bool hid_driver_save(const hid_joy_config_t *cfg, char *path, size_t path_size)
{
  if (!has_sd || msc_active()) return false;
  char name[16];
  snprintf(name, sizeof(name), "%04x_%04x.ini", cfg->vid, cfg->pid);
  snprintf(path, path_size, "/drivers/%s", name);
  if (!sd_mkdirs("/drivers")) return false;

  // other drivers for this pad would be ambiguous: keep them as *.ini.bak
  backup_drivers(cfg->vid, cfg->pid, name);

  // the text first, then one write
  static char text[2048];
  size_t n = 0;
#define TEXT(...) n += snprintf(text + n, n < sizeof(text) ? sizeof(text) - n : 0, __VA_ARGS__)
  TEXT("[info]\nvid = %04x\npid = %04x\nname = mapped in the OSD\n", cfg->vid, cfg->pid);
  if (cfg->report_id >= 0) TEXT("report_id = %d\n", cfg->report_id);
  TEXT("\n[buttons]\n");
  for (uint8_t i = 0; i < HID_JOY_BUTTONS; i++) {
    bool first = true;
    for (uint8_t j = 0; j < cfg->count; j++) {
      if (cfg->rules[j].btn != hid_joy_btns[i]) continue;
      char rule[24];
      hid_learn_format_rule(&cfg->rules[j], rule, sizeof(rule));
      if (first) TEXT("%s = %s", hid_joy_keys[i], rule);
      else TEXT("|%s", rule);
      first = false;
    }
    if (!first) TEXT("\n");
  }
#undef TEXT
  static FIL f; // not on the stack: a FIL has its own sector buffer
  bool ok = n < sizeof(text) && sd_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS);
  if (ok) {
    ok = sd_write(&f, text, n) == n;
    if (f_close(&f) != FR_OK) ok = false;
  }
  d_printf("Driver %s %s", path, ok ? "saved" : "save failed"); d_println();
  if (ok) hid_drivers_load();
  return ok;
}

void hid_drivers_dump()
{
  const driver_table_t *t = drivers;
  if (!t || t->len == 0) {
    d_println("No drivers found");
    return;
  }
  d_printf("Loaded %d drivers", t->len); d_println();
  for (uint8_t i=0; i<t->len; i++) {
    const hid_joy_config_t *d = &t->list[i].cfg;
    d_printf("Driver %s for %04x:%04x, report id %d, %u rules:", t->list[i].file, d->vid, d->pid, d->report_id, d->count); d_println();
    for (uint8_t j=0; j<d->count; j++) {
      const hid_joy_rule_t *r = &d->rules[j];
      d_printf("Button %d byte %d type %d value %d arg %d", r->btn, r->pos, r->type, r->val, r->arg);
      d_println();
    }
  }
}
