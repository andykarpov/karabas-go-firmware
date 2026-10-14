#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "file.h"
#include "main.h"
#include "kg_format.h"

void file_seek(uint32_t pos) {
    file1.seek(pos);
}

uint8_t file_read() {
    return file1.read();
}

size_t file_read_bytes(char *buf, size_t len) {
    return file1.readBytes(buf, len);
}

int file_read_buf(char *buf, size_t len) {
    return file1.read(buf, len);
}

int file_write_buf(char *buf, size_t len) {
    return file1.write(buf, len);
}

uint16_t file_read16(uint32_t pos) {
  file_seek(pos);
  uint8_t buf[2] = {0};
  file1.readBytes(buf, sizeof(buf));
  return kg_be16(buf);
}

uint32_t file_read24(uint32_t pos) {
  file_seek(pos);
  uint8_t buf[3] = {0};
  file1.readBytes(buf, sizeof(buf));
  return kg_be24(buf);
}

uint32_t file_read32(uint32_t pos) {
  file_seek(pos);
  uint8_t buf[4] = {0};
  file1.readBytes(buf, sizeof(buf));
  return kg_be32(buf);
}

void file_get_name(char *buf, size_t len) {
  file1.getName(buf, len);
}

void file_write16(uint32_t pos, uint16_t val) {
  file_seek(pos);
  file1.write((uint8_t) (val >> 8));
  file1.write((uint8_t) val);
}
