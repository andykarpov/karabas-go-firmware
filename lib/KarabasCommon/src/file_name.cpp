#include "file_name.h"
#include <string.h>

static char to_lower(char c) {
  return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

bool kg_name_has_ext(const char *name, const char *exts) {
  size_t exts_len = strlen(exts);
  if (exts_len == 0) return true;
  size_t len = strlen(name);
  if (len < 4) return false;
  const char *ext = name + len - 4;
  for (size_t i = 0; i + 4 <= exts_len; i++) {
    size_t j = 0;
    while (j < 4 && to_lower(exts[i + j]) == to_lower(ext[j])) j++;
    if (j == 4) return true;
  }
  return false;
}

void kg_short_name(const char *name, char *out, size_t width) {
  size_t len = strlen(name);
  if (len <= width) {
    memcpy(out, name, len + 1);
    return;
  }
  if (width == 0) {
    out[0] = '\0';
    return;
  }
  // keep the extension (up to 4 chars with the dot) if a char of the base still fits
  const char *dot = strrchr(name, '.');
  size_t ext_len = dot ? len - (dot - name) : 0;
  if (dot == name || ext_len > 4 || ext_len + 2 > width) ext_len = 0;
  size_t base = width - ext_len - 1;
  memcpy(out, name, base);
  out[base] = '~';
  memcpy(out + base + 1, name + len - ext_len, ext_len);
  out[width] = '\0';
}
