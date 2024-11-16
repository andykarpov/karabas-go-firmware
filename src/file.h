#pragma once

#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"

void file_seek(uint32_t pos, bool is_flash);
uint8_t file_read(bool is_flash);
size_t file_read_bytes(char *buf, size_t len, bool is_flash);
int file_read_buf(char *buf, size_t len, bool is_flash);
int file_write_buf(char *buf, size_t len, bool is_flash);
uint16_t file_read16(uint32_t pos, bool is_flash);
uint32_t file_read24(uint32_t pos, bool is_flash);
uint32_t file_read32(uint32_t pos, bool is_flash);
void file_get_name(char *buf, size_t len, bool is_flash);
bool is_flashfs(const char* filename);
void file_write16(uint32_t pos, uint16_t val, bool is_flash);
