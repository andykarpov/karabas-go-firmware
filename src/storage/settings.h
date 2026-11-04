#pragma once

#include <stddef.h>
#include "types.h"
#include "ini_update.h"

#ifdef __cplusplus
extern "C" {
#endif

extern setup_t hw_setup;

void load_setup();
// writes the values of the OSD Settings screens that differ from hw_setup into karabas.ini,
// keeping the rest of the file, then sets hw_setup to s
bool save_setup(const setup_t *s);

// one value of karabas.ini outside setup_t (APP only): read from the file on the SD card, and
// written into it (false if key=value does not fit in a line, or the save failed)
bool settings_ini_get(const char *section, const char *key, char *buf, size_t size);
bool settings_ini_set(const char *section, const char *key, const char *value);

// writes the items (kg_ini_update) into the ini file path through tmp, keeping the rest of it;
// false if a line of the old file is longer than line_max, or the save failed (APP only: the
// shared FIL, sd_tmp_open)
bool ini_file_update(const char *path, const char *tmp, kg_ini_item_t *items, uint8_t count, size_t line_max);

#ifdef __cplusplus
}
#endif
