#pragma once

// Core file (.kg1/.kg2/.kg3) format helpers. Pure C, no Arduino dependencies.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ROM block header size: 4 bytes length (bit 31 = external) + 4 bytes address, big endian
#define KG_ROM_BLOCK_HEADER_SIZE 8

typedef struct {
  uint32_t len;   // payload length: rom data, or filename length for an external rom
  uint32_t addr;  // rom load address on the fpga side
  bool external;  // payload is a filename of the rom on the sd card
} kg_rom_block_t;

uint16_t kg_be16(const uint8_t *buf);
uint32_t kg_be24(const uint8_t *buf);
uint32_t kg_be32(const uint8_t *buf);

// Parse a rom block header. remaining = bytes left in the roms section (incl. this header).
// Returns false when the block does not fit into the remaining section (broken core file).
bool kg_rom_block_parse(const uint8_t *hdr, uint32_t remaining, kg_rom_block_t *out);

#ifdef __cplusplus
}
#endif
