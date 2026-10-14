#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "hid_app.h"
#include <SegaController.h>
#include "IniFile.h"

hid_joy_config_t joy_drivers[MAX_JOY_DRIVERS];
uint8_t joy_drivers_len;

hid_joy_config_t hid_driver_load(char* filename)
{
  hid_joy_config_t cfg = {0};
  const size_t bufferLen = 80;
  char buffer[bufferLen];
  char fullname[64];
  String s("/drivers/");
  s = s+filename;
  s.toCharArray(fullname, sizeof(fullname));
  IniFile ini(fullname);
  if (ini.open()) {
    // validate file
    if (!ini.validate(buffer, bufferLen)) {
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
    if (has_sd) {
    sd1.chvol();
    if (root1.isOpen()) {
      root1.close();
    }
    if (root1.open(&sd1, "/drivers")) {
      root1.rewind();
      while (file1.openNext(&root1, O_RDONLY)) {
        char filename[32]; file1.getName(filename, sizeof(filename));
        uint8_t len = strlen(filename);
        if (len > 4 && joy_drivers_len < MAX_JOY_DRIVERS && strstr(strlwr(filename + (len - 4)), ".ini")) {
          joy_drivers[joy_drivers_len] = hid_driver_load(filename);
          joy_drivers_len++;
        }
        file1.close();
      }
    }
  }
}

void hid_drivers_dump()
{
      if (joy_drivers_len > 0) {
    d_printf("Loaded %d drivers", joy_drivers_len); d_println();
    for (uint8_t i=0; i<joy_drivers_len; i++) {
      d_printf("Driver for %04x:%04x:", joy_drivers[i].vid, joy_drivers[i].pid); d_println();
      for (uint8_t j=0; j<12; j++) {
        if (joy_drivers[i].buttons[j].isAvail) {
          if (joy_drivers[i].buttons[j].isBit) {
            d_printf("Button %d byte %d bit %d", 
              joy_drivers[i].buttons[j].btn, 
              joy_drivers[i].buttons[j].pos,
              joy_drivers[i].buttons[j].val
              );
          } else {
            d_printf("Button %d byte %d value %d", 
              joy_drivers[i].buttons[j].btn, 
              joy_drivers[i].buttons[j].pos,
              joy_drivers[i].buttons[j].val
              );
          }
          d_println();
        }
      }
    }
  } else {
    d_println("No drivers found");
  }
}