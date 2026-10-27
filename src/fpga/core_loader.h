#pragma once

#include <Arduino.h>
#include "types.h"

extern core_item_t core;
extern core_file_slot_t file_slots[MAX_FILE_SLOTS];

// configure the fpga with a core file and load its settings and roms
void do_configure(const char* filename);
void read_core(const char* filename);
void read_roms(const char* filename);

// CMD_FLASHBOOT handler: reload the current core (zx next hard reset)
void flashboot(uint8_t data);

void core_send(uint8_t pos);
// the F osd item pos can be a physical floppy drive (its flag, and the board has the connector)
bool core_osd_can_phys(uint8_t pos);
// filename is a physical drive (":phys0" / ":phys1") that the F osd item pos can be
bool core_osd_phys(uint8_t pos, const char *filename);
// send the file of an ioctl slot (FL osd item: dir + filename) to the fpga; with a progress popup
// when it was chosen in the osd
void core_ioctl_send(uint8_t slot, bool popup);
void core_send_all();
void core_trigger(uint8_t pos);
