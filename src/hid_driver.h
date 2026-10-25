#pragma once

#include <Arduino.h>
#include "hid_joy_map.h"

// load /drivers/*.ini (APP, once at boot)
void hid_drivers_load();
void hid_drivers_dump();
// the driver of a device, nullptr if there is none (any task; drivers never change once loaded)
const hid_joy_config_t *hid_driver_find(uint16_t vid, uint16_t pid);
