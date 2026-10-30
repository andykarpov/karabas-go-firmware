#include "file_name.h"
#include <stdio.h>
#include <string.h>

static char to_lower(char c) {
  return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

static bool is_ext_sep(char c) {
  return c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// the next extension of a list: its start (the dot skipped) and length, 0 at the end
static size_t next_ext(const char **p, const char **ext) {
  while (is_ext_sep(**p)) (*p)++;
  if (**p == '.') (*p)++;
  *ext = *p;
  while (**p && !is_ext_sep(**p)) (*p)++;
  return *p - *ext;
}

bool kg_name_has_ext(const char *name, const char *exts) {
  size_t len = strlen(name);
  bool any = false;
  const char *p = exts, *ext;
  size_t n;
  while (*p) {
    if ((n = next_ext(&p, &ext)) == 0) continue;
    any = true;
    // the name ends with "." + ext, and has a base before the dot
    if (len < n + 2 || name[len - n - 1] != '.') continue;
    size_t j = 0;
    while (j < n && to_lower(name[len - n + j]) == to_lower(ext[j])) j++;
    if (j == n) return true;
  }
  return !any;
}

void kg_exts_text(const char *exts, char *out, size_t width) {
  size_t w = 0;
  const char *p = exts, *ext;
  size_t n;
  out[0] = '\0';
  while (*p) {
    if ((n = next_ext(&p, &ext)) == 0) continue;
    size_t need = (w ? 1 : 0) + 1 + n;
    if (w + need > width) {
      // cut: " .." in place of the extensions that do not fit, whole ones dropped for it
      while (w > 0 && w + 3 > width) {
        while (w > 0 && out[w - 1] != ' ') w--;
        if (w > 0) w--;
      }
      if (w > 0) out[w++] = ' ';
      if (w + 2 <= width) { out[w++] = '.'; out[w++] = '.'; }
      out[w] = '\0';
      return;
    }
    if (w) out[w++] = ' ';
    out[w++] = '.';
    memcpy(out + w, ext, n);
    w += n;
    out[w] = '\0';
  }
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
