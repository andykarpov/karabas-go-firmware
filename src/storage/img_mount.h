#pragma once

#include <stdint.h>

// File mounter (`F` OSD items): the image file of a slot stays open, and the core reads and
// writes its 512-byte sectors through the IMG_* commands (docs/protocol.md, section "File
// mounter").

#define IMG_SECTOR_SIZE 512

// IMG_SEC from the fpga: the request
#define IMG_REQ_READ 1
#define IMG_REQ_WRITE 2
// IMG_SEC to the fpga: the result
#define IMG_RES_DONE 1
#define IMG_RES_ERROR 2

void img_register_handlers();
// open the file of the slot (dir + filename, if is_mounted) and send its size to the fpga;
// a slot without a file is sent with size 0 (no image / ejected)
void img_mount(uint8_t slot);
// close the files of all slots (another core is loaded)
void img_close_all();
// serve a sector request of the fpga, called from the main loop
void img_tick();
