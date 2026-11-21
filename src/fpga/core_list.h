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

#ifdef __cplusplus
}
#endif
