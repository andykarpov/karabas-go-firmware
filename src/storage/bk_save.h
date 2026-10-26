#pragma once

#include <stdint.h>

// Backup RAM files of the core (the battery RAM of a cartridge: docs/protocol.md, section "Backup
// RAM"). The core asks for its file with CMD_BK_MOUNT after a game is loaded; the firmware opens or
// creates <save dir>/<game name>.sav and mounts it in the img slot the core named, and the core
// reads and writes its sectors through the file mounter (img_mount). Before another game, another
// core or a reboot, bk_flush() makes the core write what it changed (CMD_IMG_FLUSH).

void bk_register_handlers();
// the game the file loader is sending (its dir and long file name): the name of its save
void bk_set_game(const char *dir, const char *name);
// mount a save the core asked for, called from the main loop
void bk_tick();
// the core writes its dirty sectors now (cores with CAPS_BK and a mounted save); false if it
// answered an error or not at all
bool bk_flush();
// unmount the save (the core gets size 0); after bk_flush()
void bk_close();
// another core: forget the save without telling the fpga (its files are closed by img_close_all)
void bk_forget();
