#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "hid_app.h"
#include <SegaController.h>
#include "IniFile.h"

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

    String keys[12] = {"up", "down", "left", "right", "a", "b", "c", "x", "y", "z", "start", "mode"};
    uint16_t btns[12] = {SC_BTN_UP, SC_BTN_DOWN, SC_BTN_LEFT, SC_BTN_RIGHT, SC_BTN_A, SC_BTN_B, SC_BTN_C, SC_BTN_X, SC_BTN_Y, SC_BTN_Z, SC_BTN_START, SC_BTN_MODE};

    char key[10];
    for(uint8_t i=0; i<12; i++) {
      keys[i].toCharArray(key, sizeof(key));
      cfg.buttons[i].btn = btns[i];
      cfg.buttons[i].isAvail = false;
      //d_printf("Loading %s", key); d_println();
      if (ini.getValue("buttons", key, buffer, bufferLen)) {
        //d_printf("Parsed %d = %s", i, buffer); d_println();
         cfg.buttons[i].isAvail = true;
        // parse comma separated values
        char *ptr = NULL;
        char *strings[4];
        uint8_t idx = 0;
         ptr = strtok(buffer, ",");  // delimiter
         while (ptr != NULL)
         {
           strings[idx] = ptr;
           idx++;
           ptr = strtok(NULL, ",");
         }
        // split strings buffer into values
         cfg.buttons[i].pos = atoi(strings[0]);
         cfg.buttons[i].isBit = (idx > 0 && atoi(strings[1]) == 1);
         cfg.buttons[i].val = (idx > 1) ? atoi(strings[2]) : 0;
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
    if (root1.open(&sd1, "/drivers")) {
      root1.rewind();
      while (file1.openNext(&root1, O_RDONLY)) {
        char filename[32]; file1.getName(filename, sizeof(filename));
        uint8_t len = strlen(filename);
        if (strstr(strlwr(filename + (len - 4)), ".ini")) {
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
      d_printf("Driver for %d:%d:", joy_drivers[i].vid, joy_drivers[i].pid); d_println();
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