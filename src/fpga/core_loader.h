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
// send the file of an ioctl slot (FL osd item: dir + filename) to the fpga; with a progress popup
// when it was chosen in the osd
void core_ioctl_send(uint8_t slot, bool popup);
void core_send_all();
void core_trigger(uint8_t pos);
uint8_t core_eeprom_get(uint8_t pos);
void core_eeprom_set(uint8_t pos, uint8_t val);
