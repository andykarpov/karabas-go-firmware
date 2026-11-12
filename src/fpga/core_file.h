#pragma once

// The two core file formats behind one interface:
// - kgo1 (.kg1 / .kg2 / .kg3, CORE_EXT): one board, fixed offsets (FILE_POS_*), the user's state
//   written into the file;
// - kgo2 (.kg, karabas-go-tools docs/kgo2.md): several boards, sections found through a table
//   (lib/common kg2_format), the file is only read.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "types.h"
#include "kg2_format.h"
#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CORE_FORMAT_KGO1 1
#define CORE_FORMAT_KGO2 2

#define KG2_EXT ".kg"
#define FILENAME_BOOT_KG2 "boot" KG2_EXT

typedef struct {
  uint8_t format;           // CORE_FORMAT_*
  uint8_t link;             // the MCU link of the core: 1, 2 (kgo2 BITSTREAM_LINK2)
  uint32_t bitstream_ofs, bitstream_len;
  uint32_t roms_ofs, roms_len;
  kg2_layout_t kg2;         // kgo2: the sections of this board
} core_file_t;

// a core file by its name: .kg or the CORE_EXT of the board
bool core_file_is_core(const char *name);
// the format and the sections of the open file f (name: its name, for the extension); false if
// it is not a core file of this board (a .kg without its bitstream, a broken one)
bool core_file_open(FIL *f, const char *name, core_file_t *cf);
// the fields of the core list: id, name, build, visible, order, type (filename is left alone)
void core_file_info(FIL *f, const core_file_t *cf, core_list_item_t *out);
// kgo2: the string ref of the STRINGS section, up to size - 1 chars ("" if out of the section)
void core_file_str(FIL *f, const core_file_t *cf, uint16_t ref, char *out, size_t size);
// kgo2: len bytes at ofs of a section of this board; false if it has none or they are out of it
bool core_file_sec_read(FIL *f, const core_file_t *cf, int type, uint32_t ofs, void *buf, size_t len);
// the boot core of the board: boot.kg if it has this board, else boot.kg1 / .kg2 / .kg3
const char *core_file_boot(void);

#ifdef __cplusplus
}
#endif
