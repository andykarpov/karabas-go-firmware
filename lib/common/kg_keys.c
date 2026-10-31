#include "kg_keys.h"
#include "hid_ascii.h"
#include <stddef.h>
#include <string.h>
#include <strings.h>

typedef struct { const char *name; uint8_t code; } kg_key_name_t;

static const kg_key_name_t mods[] = {
  { "ctrl", 0x01 }, { "shift", 0x02 }, { "alt", 0x04 }, { "menu", 0x08 }, { "win", 0x08 },
  { "gui", 0x08 }, { "rctrl", 0x10 }, { "rshift", 0x20 }, { "ralt", 0x40 },
};

static const kg_key_name_t keys[] = {
  { "enter", 0x28 }, { "esc", 0x29 }, { "bksp", 0x2a }, { "tab", 0x2b }, { "space", 0x2c },
  { "minus", 0x2d }, { "equal", 0x2e }, { "lbracket", 0x2f }, { "rbracket", 0x30 },
  { "backslash", 0x31 }, { "semicolon", 0x33 }, { "quote", 0x34 }, { "grave", 0x35 },
  { "comma", 0x36 }, { "dot", 0x37 }, { "slash", 0x38 }, { "caps", 0x39 },
  { "prtsc", 0x46 }, { "scrlk", 0x47 }, { "pause", 0x48 }, { "ins", 0x49 }, { "home", 0x4a },
  { "pgup", 0x4b }, { "del", 0x4c }, { "end", 0x4d }, { "pgdn", 0x4e }, { "right", 0x4f },
  { "left", 0x50 }, { "down", 0x51 }, { "up", 0x52 }, { "num", 0x53 },
};

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// one name of len chars: a modifier (its bit in *mod) or the key (*key, only one)
static bool part(const char *s, size_t len, uint8_t *mod, uint8_t *key) {
  for (size_t i = 0; i < sizeof(mods) / sizeof(mods[0]); i++)
    if (strlen(mods[i].name) == len && strncasecmp(s, mods[i].name, len) == 0) {
      *mod |= mods[i].code;
      return true;
    }
  if (*key != 0) return false;
  if (len == 1) {
    char c = s[0];
    if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
    if (c >= 'a' && c <= 'z') { *key = (uint8_t) (0x04 + (c - 'a')); return true; }
    if (c >= '1' && c <= '9') { *key = (uint8_t) (0x1e + (c - '1')); return true; }
    if (c == '0') { *key = 0x27; return true; }
    return false;
  }
  if ((s[0] == 'f' || s[0] == 'F') && len <= 3) {
    int n = 0;
    for (size_t i = 1; i < len; i++) {
      if (s[i] < '0' || s[i] > '9') { n = 0; break; }
      n = n * 10 + (s[i] - '0');
    }
    if (n >= 1 && n <= 12) { *key = (uint8_t) (0x3a + n - 1); return true; }
  }
  if (len == 4 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    int h = hex_digit(s[2]), l = hex_digit(s[3]);
    if (h >= 0 && l >= 0 && (h || l)) { *key = (uint8_t) (h * 16 + l); return true; }
    return false;
  }
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    if (strlen(keys[i].name) == len && strncasecmp(s, keys[i].name, len) == 0) {
      *key = keys[i].code;
      return true;
    }
  return false;
}

bool kg_key_parse(const char *s, uint8_t *mod, uint8_t *key) {
  *mod = 0;
  *key = 0;
  if (!s || !*s) return false;
  while (*s) {
    const char *e = strchr(s, '+');
    size_t len = e ? (size_t) (e - s) : strlen(s);
    if (len == 0 || !part(s, len, mod, key)) return false;
    if (!e) break;
    s = e + 1;
    if (!*s) return false;
  }
  return *mod != 0 || *key != 0;
}

bool kg_key_ascii(char c, uint8_t *mod, uint8_t *key) {
  if (c == 0) return false;
  for (int shift = 0; shift <= 1; shift++)
    for (uint8_t k = 0x04; k <= 0x38; k++)
      if (kg_hid_ascii(k, shift) == c) {
        *mod = shift ? 0x02 : 0x00;
        *key = k;
        return true;
      }
  return false;
}
