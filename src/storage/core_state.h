#pragma once

// The user's state of a kgo2 core (karabas-go-tools docs/kgo2.md), kept out of the core file:
// /config/<core id>.ini   [osd] key=option key (S items whose value is not the default),
//                         [file] key=path of an F / FL item ("" ejected, ":phys0" a drive),
//                         [loader] file=the last file of the file loader, [core] build=
// /config/<core id>.eep   the EEPROM of banks 4..254 (256 bytes)
// Values are matched by key: a key the core does not have, or a value it does not know, stays
// in the file untouched; a key not in the file is the default of the core. Changes are written
// 2 s after the last one, and by core_state_flush() (core_eeprom_flush(): another core, reboots).
// APP only.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CORE_STATE_DIR "/config"

// kg2_read(): a new core, its option keys follow
void core_state_begin(void);
// the key of option j of the osd item pos (kg2_read)
void core_state_opt_key(uint8_t pos, uint8_t j, const char *key);
// after kg2_read(): the user's values from the ini file and the eeprom from the .eep file
void core_state_load(void);
// a switch or an image changed (app_core_save of a kgo2 core): written later
void core_state_changed(void);
// the file loader sent this file (full path): written later
void core_state_loader_file(const char *path);
// the eeprom of the core (256 bytes) into the .eep file; false if the write failed
bool core_state_eep_save(const uint8_t *data);
// from the main loop: writes the changes 2 s after the last one
void core_state_tick(void);
// writes the changes now; false if that failed (they stay pending)
bool core_state_flush(void);
// deletes the ini and .eep files of the core (Reset settings); false if one is left
bool core_state_reset(void);

#ifdef __cplusplus
}
#endif
