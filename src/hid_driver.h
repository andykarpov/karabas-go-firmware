#pragma once

#include <Arduino.h>
#include "hid_joy_map.h"

// load /drivers/*.ini (APP, once at boot)
void hid_drivers_load();
void hid_drivers_dump();
// copy the driver of a device into cfg; false if there is none (any task)
bool hid_driver_find(uint16_t vid, uint16_t pid, hid_joy_config_t *cfg);
