#include "crc32.h"

static uint32_t crc_table[256];
static bool crc_table_ready = false;

static void crc_table_init() {
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    crc_table[i] = c;
  }
  crc_table_ready = true;
}

uint32_t kg_crc32_update(uint32_t crc, const uint8_t *buf, size_t len) {
  if (!crc_table_ready) crc_table_init();
  for (size_t i = 0; i < len; i++) crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
  return crc;
}
