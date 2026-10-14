#include "kg_format.h"

uint16_t kg_be16(const uint8_t *buf) {
  return ((uint16_t) buf[0] << 8) | buf[1];
}

uint32_t kg_be24(const uint8_t *buf) {
  return ((uint32_t) buf[0] << 16) | ((uint32_t) buf[1] << 8) | buf[2];
}

uint32_t kg_be32(const uint8_t *buf) {
  return ((uint32_t) buf[0] << 24) | ((uint32_t) buf[1] << 16) | ((uint32_t) buf[2] << 8) | buf[3];
}

bool kg_rom_block_parse(const uint8_t *hdr, uint32_t remaining, kg_rom_block_t *out) {
  uint32_t len = kg_be32(hdr);
  out->external = (len & 0x80000000) != 0;
  out->len = len & 0x7FFFFFFF;
  out->addr = kg_be32(hdr + 4);
  if (remaining < KG_ROM_BLOCK_HEADER_SIZE) return false;
  return out->len <= remaining - KG_ROM_BLOCK_HEADER_SIZE;
}
