#pragma once

// CRC-32 (IEEE 802.3, the one of zip / `crc32` / `cksum -a crc32b`). Pure C++, no Arduino
// dependencies. Used to check that a file reached the fpga as it is on the sd card.

#include <stddef.h>
#include <stdint.h>

#define KG_CRC32_INIT 0xFFFFFFFFu

// Feed len bytes into a running crc (start with KG_CRC32_INIT).
uint32_t kg_crc32_update(uint32_t crc, const uint8_t *buf, size_t len);

// The final value of a running crc.
static inline uint32_t kg_crc32_final(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }
