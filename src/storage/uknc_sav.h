#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// "Run SAV" of the UKNC core (core.id "uknc"): a .SAV needs a running RT-11, so the FL item of
// slot 5 sends no file to the core. The firmware copies /mc0511-media/RT11BASE.DSK (or
// BASERT11.DSK, the name of tang-uknc; never written) to RT11SAV.DSK, adds the program with lib/common rt11_volume, makes STARTS.COM run it, mounts the
// disk in FDD0 and resets the machine. The generated disk before it stays as RT11OLD.DSK.

// the item of this slot is Run SAV in the running core
bool uknc_sav_slot(uint8_t slot);
// the file chosen in the slot: false, with a message in the OSD, if nothing was changed
bool uknc_sav_run(uint8_t slot);

#ifdef __cplusplus
}
#endif
