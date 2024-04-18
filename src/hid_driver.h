#pragma once

#include <Arduino.h>
#include "hid_app.h"
#include "config.h"
#include "types.h"

hid_joy_config_t hid_driver_load(char* filename);
void hid_drivers_load();
void hid_drivers_dump();
