#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"
#include "types.h"
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

void file_seek(uint32_t pos);
uint8_t file_read();
size_t file_read_bytes(char *buf, size_t len);
size_t file_write_bytes(const char *buf, size_t len);
uint16_t file_read16(uint32_t pos);
uint32_t file_read24(uint32_t pos);
uint32_t file_read32(uint32_t pos);
void file_write16(uint32_t pos, uint16_t val);

#ifdef __cplusplus
}
#endif
