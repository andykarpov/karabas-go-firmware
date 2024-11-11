#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "file.h"
#include "main.h"

void file_seek(uint32_t pos, bool is_flash) {
  if (is_flash) {
    ffile.seek(pos);
  } else {
    file1.seek(pos);
  }
}

uint8_t file_read(bool is_flash) {
  if (is_flash) {
    return ffile.read();
  } else {
    return file1.read();
  }
}

size_t file_read_bytes(char *buf, size_t len, bool is_flash) {
  if (is_flash) {
    return ffile.readBytes(buf, len);
  } else {
    return file1.readBytes(buf, len);
  }
}

int file_read_buf(char *buf, size_t len, bool is_flash) {
  if (is_flash) {
    return ffile.readBytes(buf, len);
  } else {
    return file1.read(buf, len);
  }
}

uint16_t file_read16(uint32_t pos, bool is_flash) {
  file_seek(pos, is_flash);
  
  uint16_t res = 0;

  if (is_flash) {
    char buf[2] = {0};
    ffile.readBytes(buf, sizeof(buf));
    res = buf[1] + buf[0]*256;
  } else {
    uint8_t buf[2] = {0};
    file1.readBytes(buf, sizeof(buf));
    res = buf[1] + buf[0]*256;
  }
  
  return res;
}

uint32_t file_read24(uint32_t pos, bool is_flash) {
  file_seek(pos, is_flash);

  uint32_t res = 0;

  if (is_flash) {
    char buf[3] = {0};
    ffile.readBytes(buf, sizeof(buf));
    res = buf[2] + buf[1]*256 + buf[0]*256*256;
  } else {
    uint8_t buf[3] = {0};
    file1.readBytes(buf, sizeof(buf));
    res = buf[2] + buf[1]*256 + buf[0]*256*256;
  }

  return res;
}

uint32_t file_read32(uint32_t pos, bool is_flash) {
  file_seek(pos, is_flash);

  uint32_t res = 0;
  if (is_flash) {
    char buf[4] = {0};
    ffile.readBytes(buf, sizeof(buf));
    res = buf[3] + buf[2]*256 + buf[1]*256*256 + buf[0]*256*256*256;
  } else {
    uint8_t buf[4] = {0};
    file1.readBytes(buf, sizeof(buf));
    res = buf[3] + buf[2]*256 + buf[1]*256*256 + buf[0]*256*256*256;
  }

  return res;
}

void file_get_name(char *buf, size_t len, bool is_flash) {
  if (is_flash) {
    String s = String(ffile.fullName());
    s = "/" + s;
    s.toCharArray(buf, len);
  } else {
    file1.getName(buf, sizeof(buf));
  }
}

bool is_flashfs(const char* filename) {
  String s = String(filename);
  return (s.charAt(0) == '/');
}

void file_write16(uint32_t pos, uint16_t val, bool is_flash) {
  file_seek(pos, is_flash);
  if (is_flash) {
    file_seek(pos, is_flash);
    ffile.write((uint8_t) val >> 8);
    file_seek(pos+1, is_flash);
    ffile.write((uint8_t) val);
  } else {
    file_seek(pos, is_flash);
    file1.write((uint8_t) val >> 8);
    file_seek(pos+1, is_flash);
    file1.write((uint8_t) val);
  }
}
