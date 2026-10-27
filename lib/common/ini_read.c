#include "ini_read.h"
#include <string.h>
#include <strings.h>

static bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

typedef enum { LINE_OK, LINE_LAST, LINE_TOO_LONG, LINE_END } line_t;

// the line at *pos (start, n chars without its end), as IniFile::readLine with a buffer of
// buf_len bytes (0: no limit); *pos moves past the line end
static line_t next_line(const char *text, size_t len, size_t buf_len, size_t *pos,
                        const char **start, size_t *n) {
  if (*pos >= len) return LINE_END;
  const char *s = text + *pos;
  size_t left = len - *pos;
  size_t scan = buf_len ? (left < buf_len - 1 ? left : buf_len - 1) : left;
  *start = s;
  for (size_t i = 0; i < scan; i++) {
    if (s[i] == '\n' || s[i] == '\r') {
      char other = s[i] == '\n' ? '\r' : '\n';
      *n = i;
      *pos += i + 1;
      if (i + 1 < left && s[i + 1] == other) (*pos)++;
      return LINE_OK;
    }
  }
  if (!buf_len || left <= buf_len) {
    // the end of the file without a line end
    *n = left;
    while (*n && (s[*n - 1] == '\n' || s[*n - 1] == '\r')) (*n)--;
    *pos = len;
    return LINE_LAST;
  }
  return LINE_TOO_LONG;
}

bool kg_ini_valid(const char *text, size_t len, size_t buf_len) {
  if (buf_len < 3) return false;
  size_t pos = 0, n;
  const char *s;
  for (;;) {
    line_t r = next_line(text, len, buf_len, &pos, &s, &n);
    if (r == LINE_TOO_LONG) return false;
    if (r != LINE_OK) return true;
  }
}

// s[0..n) without the spaces around it
static void trim(const char **s, size_t *n) {
  while (*n && is_space(**s)) { (*s)++; (*n)--; }
  while (*n && is_space((*s)[*n - 1])) (*n)--;
}

static bool same(const char *s, size_t n, const char *name) {
  return strlen(name) == n && strncasecmp(s, name, n) == 0;
}

bool kg_ini_get(const char *text, size_t len, const char *section, const char *key,
                char *out, size_t out_size) {
  size_t pos = 0, n;
  const char *s;
  bool in_section = false;
  for (;;) {
    line_t r = next_line(text, len, 0, &pos, &s, &n);
    if (r == LINE_END) return false;
    trim(&s, &n);
    if (n && (s[0] == ';' || s[0] == '#')) {
      // a comment
    } else if (n && s[0] == '[') {
      if (in_section) return false; // the next section: the key is not in ours
      const char *e = memchr(s, ']', n);
      if (e) {
        const char *name = s + 1;
        size_t nn = e - name;
        trim(&name, &nn);
        in_section = same(name, nn, section);
      }
    } else if (in_section) {
      const char *eq = memchr(s, '=', n);
      if (eq) {
        const char *k = s;
        size_t kn = eq - s;
        trim(&k, &kn);
        if (same(k, kn, key)) {
          const char *v = eq + 1;
          size_t vn = n - (v - s);
          trim(&v, &vn);
          if (vn + 1 > out_size) return false;
          memcpy(out, v, vn);
          out[vn] = '\0';
          return true;
        }
      }
    }
    if (r == LINE_LAST) return false;
  }
}

bool kg_ini_get_bool(const char *text, size_t len, const char *section, const char *key, bool *val) {
  char v[8];
  if (!kg_ini_get(text, len, section, key, v, sizeof(v))) return false;
  if (!strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcmp(v, "1")) { *val = true; return true; }
  if (!strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcmp(v, "0")) { *val = false; return true; }
  return false;
}
