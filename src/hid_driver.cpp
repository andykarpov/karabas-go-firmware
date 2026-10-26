#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "hid_driver.h"
#include "hid_learn.h"
#include <SegaController.h>
#include "IniFile.h"
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

static bool is_driver_file(FsFile &f, char *filename, size_t size)
{
  f.getName(filename, size);
  size_t len = strlen(filename);
  return !f.isDir() && len > 4 && strcasecmp(filename + len - 4, ".ini") == 0;
}

static hid_joy_config_t hid_driver_load(const char *filename)
{
  hid_joy_config_t cfg = {0};
  cfg.report_id = -1;
  const size_t bufferLen = 80;
  char buffer[bufferLen];
  char fullname[64];
  snprintf(fullname, sizeof(fullname), "/drivers/%s", filename);
  IniFile ini(fullname);
  if (ini.open()) {
    // validate file
    if (!ini.validate(buffer, bufferLen)) {
      d_printf("%s is invalid", fullname); d_println();
      ini.close();
      return cfg;
    }

    cfg.vid = (ini.getValue("info", "vid", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : 0;
    cfg.pid = (ini.getValue("info", "pid", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : 0;
    if (ini.getValue("info", "report_id", buffer, bufferLen)) cfg.report_id = (uint8_t) strtoul(buffer, 0, 10);

    for(uint8_t i=0; i<HID_JOY_BUTTONS; i++) {
      if (ini.getValue("buttons", hid_joy_keys[i], buffer, bufferLen)) {
        if (!hid_joy_rules_parse(buffer, hid_joy_btns[i], &cfg)) {
          d_printf("%s: invalid mapping for %s: %s", filename, hid_joy_keys[i], buffer); d_println();
        }
      }
    }

    ini.close();
  }
  return cfg;
}

void hid_drivers_load()
{
  if (!has_sd) return;
  sd1.chvol();
  FsFile dir, f;
  if (!dir.open(&sd1, "/drivers")) return;

  char filename[32];
  uint16_t count = 0;
  while (f.openNext(&dir, O_RDONLY)) {
    if (is_driver_file(f, filename, sizeof(filename))) count++;
    f.close();
  }
  if (count > MAX_JOY_DRIVERS) count = MAX_JOY_DRIVERS;

  driver_table_t *t = new driver_table_t;
  t->len = 0;
  t->list = count ? new driver_t[count] : nullptr;
  dir.rewind();
  while (t->len < count && f.openNext(&dir, O_RDONLY)) {
    bool ok = is_driver_file(f, filename, sizeof(filename));
    f.close();
    if (!ok) continue;
    hid_joy_config_t cfg = hid_driver_load(filename);
    if (cfg.vid || cfg.pid) {
      t->list[t->len].cfg = cfg;
      strlcpy(t->list[t->len].file, filename, sizeof(t->list[t->len].file));
      t->len++;
    }
  }
  dir.close();

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
  FsFile dir, f;
  if (dir.open(&sd1, "/drivers")) {
    char filename[32];
    while (n_others < 4 && f.openNext(&dir, O_RDONLY)) {
      bool ok = is_driver_file(f, filename, sizeof(filename)) && (!keep || strcasecmp(filename, keep) != 0);
      f.close();
      if (!ok) continue;
      hid_joy_config_t old = hid_driver_load(filename);
      if (old.vid == vid && old.pid == pid) strcpy(others[n_others++], filename);
    }
    dir.close();
  }
  for (uint8_t i = 0; i < n_others; i++) {
    char from[64], to[64];
    snprintf(from, sizeof(from), "/drivers/%s", others[i]);
    snprintf(to, sizeof(to), "/drivers/%s.bak", others[i]);
    if (sd1.exists(to)) sd1.remove(to);
    d_printf("Driver %s renamed to %s", from, to); d_println();
    sd1.rename(from, to);
  }
  return n_others;
}

bool hid_driver_forget(uint16_t vid, uint16_t pid)
{
  if (!has_sd || msc_active()) return false;
  sd1.chvol();
  bool any = backup_drivers(vid, pid, nullptr) > 0;
  hid_drivers_load();
  return any;
}

bool hid_driver_save(const hid_joy_config_t *cfg, char *path, size_t path_size)
{
  if (!has_sd || msc_active()) return false;
  sd1.chvol();
  char name[16];
  snprintf(name, sizeof(name), "%04x_%04x.ini", cfg->vid, cfg->pid);
  snprintf(path, path_size, "/drivers/%s", name);
  if (!sd1.exists("/drivers") && !sd1.mkdir("/drivers")) return false;

  // other drivers for this pad would be ambiguous: keep them as *.ini.bak
  backup_drivers(cfg->vid, cfg->pid, name);

  FsFile f;
  if (!f.open(&sd1, path, O_WRONLY | O_CREAT | O_TRUNC)) return false;
  f.printf("[info]\nvid = %04x\npid = %04x\nname = mapped in the OSD\n", cfg->vid, cfg->pid);
  if (cfg->report_id >= 0) f.printf("report_id = %d\n", cfg->report_id);
  f.print("\n[buttons]\n");
  for (uint8_t i = 0; i < HID_JOY_BUTTONS; i++) {
    bool first = true;
    for (uint8_t j = 0; j < cfg->count; j++) {
      if (cfg->rules[j].btn != hid_joy_btns[i]) continue;
      char rule[24];
      hid_learn_format_rule(&cfg->rules[j], rule, sizeof(rule));
      if (first) f.printf("%s = %s", hid_joy_keys[i], rule);
      else f.printf("|%s", rule);
      first = false;
    }
    if (!first) f.print("\n");
  }
  bool ok = f.close();
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
    const hid_joy_config_t &d = t->list[i].cfg;
    d_printf("Driver %s for %04x:%04x, report id %d, %u rules:", t->list[i].file, d.vid, d.pid, d.report_id, d.count); d_println();
    for (uint8_t j=0; j<d.count; j++) {
      const hid_joy_rule_t &r = d.rules[j];
      d_printf("Button %d byte %d type %d value %d arg %d", r.btn, r.pos, r.type, r.val, r.arg);
      d_println();
    }
  }
}
