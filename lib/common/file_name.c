#include "file_name.h"
#include <stdio.h>
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

int kg_phys_drive(const char *name) {
  if (strncmp(name, ":phys", 5) != 0 || (name[5] != '0' && name[5] != '1') || name[6] != '\0') return -1;
  return name[5] - '0';
}

void kg_phys_name(int drive, char *out) {
  memcpy(out, ":phys0", 7);
  out[5] = drive ? '1' : '0';
}

static bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

void kg_trim(char *s) {
  size_t len = strlen(s);
  while (len > 0 && is_space(s[len - 1])) len--;
  s[len] = '\0';
  size_t from = 0;
  while (is_space(s[from])) from++;
  if (from) memmove(s, s + from, len - from + 1);
}

void kg_str_lower(char *s) {
  for (; *s; s++) *s = to_lower(*s);
}

bool kg_path_join(char *out, size_t size, const char *dir, const char *name) {
  int n = snprintf(out, size, "%s/%s", dir, name);
  return n >= 0 && (size_t) n < size;
}

void kg_path_collapse(char *path) {
  char *w = path;
  for (const char *r = path; *r; r++) {
    if (*r == '/' && w > path && w[-1] == '/') continue;
    *w++ = *r;
  }
  *w = '\0';
}

void kg_path_root(char *dir, size_t size) {
  if (dir[0] == '/' || size < 2) return;
  size_t len = 0;
  while (len < size - 1 && dir[len]) len++;
  if (len + 2 > size) len = size - 2;
  memmove(dir + 1, dir, len);
  dir[0] = '/';
  dir[len + 1] = '\0';
}

void kg_path_parent(char *path) {
  char *slash = strrchr(path, '/');
  if (slash) *slash = '\0';
}
