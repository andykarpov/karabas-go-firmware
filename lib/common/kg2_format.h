#pragma once

// Core file format kgo2 (.kg): one file for several boards, made by make_core (karabas-go-tools,
// docs/kgo2.md). A 32-byte header, a table of sections (type, board mask, offset, length), then
// the sections. Little-endian. The parsers take the bytes of a header / record / section read
// from the file and check every offset against the length they got.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KG2_SIGNATURE "kgo2"
#define KG2_HEADER_SIZE 32
#define KG2_ENTRY_SIZE 16      // a section table entry
#define KG2_MAX_SECTIONS 64    // more is a broken file
#define KG2_CORE_SIZE 16       // the fields of the CORE record this parser knows
#define KG2_LOADER_SIZE 12
#define KG2_OSD_HEAD_SIZE 8
#define KG2_ITEM_SIZE 48       // the fields of an OSD item record this parser knows
#define KG2_OPTION_SIZE 8
#define KG2_ROM_BLOCK_HEADER_SIZE 8
#define KG2_BOARD_BIT(hw_id) ((uint16_t) (1u << (hw_id)))

enum {
  KG2_SEC_STRINGS = 1,
  KG2_SEC_CORE,
  KG2_SEC_LOADER,
  KG2_SEC_OSD,
  KG2_SEC_BUTTONS,
  KG2_SEC_MATRIX,
  KG2_SEC_EEPROM_INIT,
  KG2_SEC_ROMS,
  KG2_SEC_BITSTREAM,
  KG2_SEC_COUNT // the types this parser knows are 1..KG2_SEC_COUNT-1
};

// flags of an OSD item
#define KG2_ITEM_PHYSICAL 0x01 // F: the physical floppy drives (Karabas Go)
#define KG2_ITEM_READONLY 0x02 // F: the image without writes
#define KG2_ITEM_AUTOLOAD 0x04 // FL: the file goes to the core at its start

#define KG2_LOADER_AUTOLOAD 0x01

uint16_t kg_le16(const uint8_t *buf);
uint32_t kg_le32(const uint8_t *buf);

typedef struct {
  uint8_t version;
  uint16_t header_size;  // the table starts here
  uint16_t count;        // sections
  uint16_t entry_size;
  uint16_t boards;       // the boards with a bitstream
} kg2_header_t;

typedef struct {
  uint32_t offset;
  uint32_t length;
  bool present;
} kg2_section_t;

// the sections of one board, by type
typedef struct {
  kg2_section_t sec[KG2_SEC_COUNT];
} kg2_layout_t;

// the first KG2_HEADER_SIZE bytes; false if not kgo2 or broken
bool kg2_header_parse(const uint8_t *buf, size_t len, kg2_header_t *h);
// the sections of the board (its KG2_BOARD_BIT) from the table (h->count entries); false if an
// entry is out of the file, or STRINGS / CORE / BITSTREAM is missing for the board
bool kg2_layout_parse(const kg2_header_t *h, const uint8_t *table, size_t len, uint32_t file_size,
                      uint16_t board_bit, kg2_layout_t *out);

// a string of the STRINGS section by its offset; "" if out of the section or not terminated
const char *kg2_str(const uint8_t *strings, size_t len, uint16_t ref);

typedef struct {
  uint16_t id, name, build; // strings
  uint8_t type;             // 0 boot, 1 osd, 2 fileloader, 255 hidden
  uint8_t visible;
  uint8_t order;
  uint8_t rtc_mode;
  uint8_t eeprom_bank;
  uint8_t spi_freq;
} kg2_core_t;

bool kg2_core_parse(const uint8_t *sec, size_t len, kg2_core_t *out);

typedef struct {
  uint16_t dir, exts, save_dir; // strings
  uint8_t flags;                // KG2_LOADER_*
  uint8_t files;                // file candidates
  uint16_t files_ofs;           // their string refs, in the section
} kg2_loader_t;

bool kg2_loader_parse(const uint8_t *sec, size_t len, kg2_loader_t *out);

// the string ref n of a list of refs at ofs of a section (the file candidates); false if out
bool kg2_ref_at(const uint8_t *sec, size_t len, uint16_t ofs, uint8_t n, uint16_t *ref);

typedef struct {
  uint16_t count;
  uint16_t item_size;
} kg2_osd_head_t;

bool kg2_osd_head_parse(const uint8_t *sec, size_t len, kg2_osd_head_t *out);

typedef struct {
  uint8_t type;        // as kgo1: 0 S, 1 N, 2 T, 3 H, 4 P, 5 F, 6 FL, 7 M, 8 separator
  uint8_t flags;       // KG2_ITEM_*
  uint8_t index;       // CMD_SWITCHES number, 255 none
  uint8_t page;
  uint8_t open_page;   // M
  uint8_t cond_item;   // the item number + 1, 0 none
  uint8_t cond_flags;  // bit 7 disable (else hide), bit 6 invert
  uint8_t cond_value;  // the option value
  uint8_t def;         // the option value
  uint8_t slot;        // F / FL
  uint8_t keys[2];     // hotkey: modifiers, key code
  uint8_t options;
  uint8_t files;
  uint16_t key, name, hotkey, exts, dir; // strings
  uint16_t options_ofs, files_ofs;       // in the section
} kg2_item_t;

// the item n (byte offset KG2_OSD_HEAD_SIZE + n * item_size)
bool kg2_item_parse(const uint8_t *sec, size_t len, const kg2_osd_head_t *head, uint16_t n, kg2_item_t *out);
// the item record alone (KG2_ITEM_SIZE bytes)
bool kg2_item_record_parse(const uint8_t *rec, size_t len, kg2_item_t *out);

typedef struct {
  uint16_t key, name; // strings
  uint8_t value;      // sent to the core
} kg2_option_t;

// the option n of the records at ofs of the section
bool kg2_option_parse(const uint8_t *sec, size_t len, uint16_t ofs, uint8_t n, kg2_option_t *out);
// one option record (KG2_OPTION_SIZE bytes)
bool kg2_option_record_parse(const uint8_t *rec, size_t len, kg2_option_t *out);

// a ROM block header (as kgo1, little-endian); remaining = bytes left in the ROMS section
typedef struct {
  uint32_t len;
  uint32_t addr;
  bool external; // the payload is a file name on the SD card
} kg2_rom_block_t;

bool kg2_rom_block_parse(const uint8_t *hdr, uint32_t remaining, kg2_rom_block_t *out);

#ifdef __cplusplus
}
#endif
