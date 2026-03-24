#pragma once

#include <Arduino.h>
#include <GyverMenu.h>
#include <GyverMenuExt.h>
#include "types.h"

void CoreItem(gm::Builder& b, core_osd_t& osd_item, void (*cb)(core_osd_t&) = nullptr);
