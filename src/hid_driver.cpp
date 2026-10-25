#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "hid_driver.h"
#include <SegaController.h>
#include "IniFile.h"
#include "hardware/sync.h"

// Loaded once by APP, read by the USB task on core1. The array is filled before the count is
// published (with a barrier), and it is never changed afterwards.
static hid_joy_config_t *joy_drivers = nullptr;
static volatile uint8_t joy_drivers_len = 0;

static bool is_driver_file(FsFile &f, char *filename, size_t size)
{
  f.getName(filename, size);
  size_t len = strlen(filename);
  return !f.isDir() && len > 4 && strcasecmp(filename + len - 4, ".ini") == 0;
}

static hid_joy_config_t hid_driver_load(const char *filename)
{
  hid_joy_config_t cfg = {0};
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

    const char* keys[HID_JOY_BUTTONS] = {"up", "down", "left", "right", "a", "b", "c", "x", "y", "z", "start", "mode"};
    uint16_t btns[HID_JOY_BUTTONS] = {SC_BTN_UP, SC_BTN_DOWN, SC_BTN_LEFT, SC_BTN_RIGHT, SC_BTN_A, SC_BTN_B, SC_BTN_C, SC_BTN_X, SC_BTN_Y, SC_BTN_Z, SC_BTN_START, SC_BTN_MODE};

    for(uint8_t i=0; i<HID_JOY_BUTTONS; i++) {
      cfg.buttons[i].btn = btns[i];
      cfg.buttons[i].isAvail = false;
      if (ini.getValue("buttons", keys[i], buffer, bufferLen)) {
        if (!hid_joy_button_parse(buffer, &cfg.buttons[i])) {
          d_printf("%s: invalid mapping for %s: %s", filename, keys[i], buffer); d_println();
        }
      }
    }

    ini.close();
  }
  return cfg;
}

void hid_drivers_load()
{
  if (!has_sd || joy_drivers) return;
  sd1.chvol();
  if (root1.isOpen()) {
    root1.close();
  }
  if (!root1.open(&sd1, "/drivers")) return;

  char filename[32];
  uint16_t count = 0;
  while (file1.openNext(&root1, O_RDONLY)) {
    if (is_driver_file(file1, filename, sizeof(filename))) count++;
    file1.close();
  }
  if (count > MAX_JOY_DRIVERS) count = MAX_JOY_DRIVERS;
  if (count == 0) { root1.close(); return; }

  hid_joy_config_t *drivers = new hid_joy_config_t[count];
  uint8_t len = 0;
  root1.rewind();
  while (len < count && file1.openNext(&root1, O_RDONLY)) {
    bool ok = is_driver_file(file1, filename, sizeof(filename));
    file1.close();
    if (!ok) continue;
    hid_joy_config_t cfg = hid_driver_load(filename);
    if (cfg.vid || cfg.pid) drivers[len++] = cfg;
  }
  root1.close();

  joy_drivers = drivers;
  __dmb();
  joy_drivers_len = len;
}

bool hid_driver_find(uint16_t vid, uint16_t pid, hid_joy_config_t *cfg)
{
  uint8_t len = joy_drivers_len;
  __dmb();
  for (uint8_t i = 0; i < len; i++) {
    if (joy_drivers[i].vid == vid && joy_drivers[i].pid == pid) {
      *cfg = joy_drivers[i];
      return true;
    }
  }
  return false;
}

void hid_drivers_dump()
{
  uint8_t len = joy_drivers_len;
  if (len == 0) {
    d_println("No drivers found");
    return;
  }
  d_printf("Loaded %d drivers", len); d_println();
  for (uint8_t i=0; i<len; i++) {
    d_printf("Driver for %04x:%04x:", joy_drivers[i].vid, joy_drivers[i].pid); d_println();
    for (uint8_t j=0; j<HID_JOY_BUTTONS; j++) {
      const hid_joy_button_config_t &b = joy_drivers[i].buttons[j];
      if (b.isAvail) {
        d_printf("Button %d byte %d %s %d", b.btn, b.pos, b.isBit ? "bit" : "value", b.val);
        d_println();
      }
    }
  }
}
