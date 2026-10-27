#include "config.h"
#include "types.h"
#include "storage/file.h"
#include "main.h"
#include "kg_format.h"

// the open core file (file1)

void file_seek(uint32_t pos) {
    f_lseek(&file1, pos);
}

uint8_t file_read() {
    uint8_t b = 0xFF;
    sd_read(&file1, &b, 1);
    return b;
}

size_t file_read_bytes(char *buf, size_t len) {
    return sd_read(&file1, buf, len);
}

int file_read_buf(char *buf, size_t len) {
    return sd_read(&file1, buf, len);
}

int file_write_buf(char *buf, size_t len) {
    return sd_write(&file1, buf, len);
}

uint16_t file_read16(uint32_t pos) {
  file_seek(pos);
  uint8_t buf[2] = {0};
  sd_read(&file1, buf, sizeof(buf));
  return kg_be16(buf);
}

uint32_t file_read24(uint32_t pos) {
  file_seek(pos);
  uint8_t buf[3] = {0};
  sd_read(&file1, buf, sizeof(buf));
  return kg_be24(buf);
}

uint32_t file_read32(uint32_t pos) {
  file_seek(pos);
  uint8_t buf[4] = {0};
  sd_read(&file1, buf, sizeof(buf));
  return kg_be32(buf);
}

void file_write16(uint32_t pos, uint16_t val) {
  file_seek(pos);
  uint8_t buf[2] = { (uint8_t) (val >> 8), (uint8_t) val };
  sd_write(&file1, buf, sizeof(buf));
}
