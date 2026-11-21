#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// The cores on the SD card (the .kg of this board in its root), sorted by [setup] core_sort: the
// list of RPC CORES for the menu of the boot core, AT+KARABAS=load.
extern core_list_item_t *cores; // on the heap, NULL while empty
extern uint8_t cores_len;

// reads the root of the card again
void core_list_read(void);
// sorts it again by hw_setup.core_sort
void core_list_sort(void);
// the header of the open core file (file1); false if it is not a core of this board
bool core_list_get_item(const char *filename, core_list_item_t *core);
// AT+KARABAS=load, RPC CORES LAUNCH: the core file starts on its screen, from the boot core or any other
void core_list_start_file(const char *filename);
// from a core back to the boot core, without an MCU reboot
void core_list_return(void);

#ifdef __cplusplus
}
#endif
