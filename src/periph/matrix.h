#pragma once

#include <Arduino.h>
#include "kg_matrix.h"

void matrix_begin();
void matrix_register_handlers();
void matrix_frame();     // one 20 ms frame, from the MATRIX task
bool matrix_present();
void matrix_progress(uint32_t val, uint32_t max);
// the settings of the core being loaded (read_core), used from matrix_on_configure_done()
void matrix_set_config(const kg_matrix_cfg_t *cfg, const char *name, const char *build);
// the image slots for the "drive" effect (img_mount): the size in sectors (0 = none), an access
void matrix_drive_mount(uint8_t slot, uint32_t sectors);
void matrix_drive_access(uint8_t slot, uint32_t lba, bool write);
// karabas.ini [matrix] enabled / brightness to show: the edited values while Settings is open
// (they show at once), hw_setup after a save or when Settings is left without one
void matrix_setup(bool enabled, uint8_t brightness);
void matrix_on_configure_start();
void matrix_on_configure_done(uint8_t core_type);

#define MATRIX_ICON_MSC 0
#define MATRIX_ICON_BOOTSEL 1
void matrix_icon(uint8_t icon); // a static icon instead of the scroll / meters (msc, bootsel)
