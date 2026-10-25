#pragma once

#include <Arduino.h>
#include "hid_joy_map.h"

// ini keys and sega bits of the 12 buttons, in the order of the ini
extern const char *const hid_joy_keys[HID_JOY_BUTTONS];
extern const uint16_t hid_joy_btns[HID_JOY_BUTTONS];
// bumped each time the drivers are (re)loaded
extern volatile uint32_t hid_drivers_gen;

// load /drivers/*.ini (APP: at boot, and again after a save)
void hid_drivers_load();
void hid_drivers_dump();
// the driver of a device, nullptr if there is none (any task; a loaded driver never changes)
const hid_joy_config_t *hid_driver_find(uint16_t vid, uint16_t pid);
// write /drivers/<vid>_<pid>.ini (other ini files of the pad become *.ini.bak) and reload the
// drivers (APP). path gets the file name.
bool hid_driver_save(const hid_joy_config_t *cfg, char *path, size_t path_size);
