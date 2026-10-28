#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

extern core_item_t core;
extern core_file_slot_t file_slots[MAX_FILE_SLOTS];

// configure the fpga with a core file and load its settings and roms
void do_configure(const char* filename);
void read_core(const char* filename);
void read_roms(const char* filename);

// CMD_FLASHBOOT handler: reload the current core (zx next hard reset)
void flashboot(uint8_t data);

void core_send(uint8_t pos);
// the osd item pos is a file item (F: the file mounter, FL: the ioctl file loader)
bool core_osd_is_file(uint8_t pos);
// the osd item pos takes the cursor of the core menu: it has options, or it is a file item
bool core_osd_selectable(uint8_t pos);
// the option of the osd item pos one up (d = 1) or down (d = -1), around the ends: sent to the
// core, a switch is saved into the core file later (core.osd_need_save)
void core_osd_change(uint8_t pos, int d);
// the F osd item pos can be a physical floppy drive (its flag, and the board has the connector)
bool core_osd_can_phys(uint8_t pos);
// filename is a physical drive (":phys0" / ":phys1") that the F osd item pos can be
bool core_osd_phys(uint8_t pos, const char *filename);
// send the file of an ioctl slot (FL osd item: dir + filename) to the fpga; with a progress popup
// when it was chosen in the osd
void core_ioctl_send(uint8_t slot, bool popup);
// len bytes of f from its position (UINT32_MAX: to its end) to the core at addr through
// link_write_data() (the ROMs, the file loader, ioctl), read into the bitstream buffer; progress
// (if any) every 32 KB and at the end, crc (if any) updated with the bytes. The bytes sent.
typedef void (*core_send_progress_t)(uint32_t sent, void *ctx);
uint32_t core_send_file(FIL *f, uint32_t len, uint8_t data_cmd, uint8_t bank_cmd, uint32_t addr,
  core_send_progress_t progress, void *ctx, uint32_t *crc);
// a core_send_progress_t: the loading popup, ctx is the size of the file (uint32_t)
void core_loading_popup(uint32_t sent, void *ctx);
void core_send_all();
void core_trigger(uint8_t pos);
// the EEPROM of banks 4..254 (in the core file): saved by the tick when the core stops changing
// it; flush before another core or a reboot. false: not saved (no card, a write error)
void core_eeprom_tick(void);
bool core_eeprom_flush(void);

#ifdef __cplusplus
}
#endif
