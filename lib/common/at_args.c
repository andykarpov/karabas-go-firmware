#include "at_args.h"
#include "hid_auto_map.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// a decimal number that ends at a comma or the end of s; false if none, or out of the range
static bool number(const char **s, int min, int max, int *out) {
  char *end;
  long v = strtol(*s, &end, 10);
  if (end == *s || v < min || v > max || (*end && *end != ',')) return false;
  *out = (int) v;
  *s = end;
  return true;
}

bool kg_at_mouse_parse(const char *s, kg_at_mouse_t *m) {
  m->wheel = 0;
  m->buttons = KG_AT_MOUSE_KEEP;
  if (!number(&s, -KG_AT_MOUSE_MAX, KG_AT_MOUSE_MAX, &m->dx) || *s++ != ',') return false;
  if (!number(&s, -KG_AT_MOUSE_MAX, KG_AT_MOUSE_MAX, &m->dy)) return false;
  if (!*s) return true;
  s++; // the comma
  if (*s != ',') {
    if (!number(&s, 0, 31, &m->buttons)) return false;
    if (!*s) return true;
  }
  s++; // the comma
  return number(&s, -127, 127, &m->wheel) && !*s;
}

int kg_at_mouse_steps(int dx, int dy) {
  int a = abs(dx), b = abs(dy), n = (a > b ? a : b);
  return n ? (n + 126) / 127 : 1;
}

int8_t kg_at_mouse_step(int *rest) {
  int v = *rest > 127 ? 127 : *rest < -127 ? -127 : *rest;
  *rest -= v;
  return (int8_t) v;
}

bool kg_at_mouse_button(const char *name, uint8_t *mask) {
  if (!*name || strcasecmp(name, "left") == 0) *mask = 1;
  else if (strcasecmp(name, "right") == 0) *mask = 2;
  else if (strcasecmp(name, "middle") == 0) *mask = 4;
  else return false;
  return true;
}

bool kg_at_pad_parse(const char *s, uint16_t *buttons) {
  static const struct { const char *name; uint16_t bit; } names[] = {
    {"up", HID_SC_UP}, {"down", HID_SC_DOWN}, {"left", HID_SC_LEFT}, {"right", HID_SC_RIGHT},
    {"start", HID_SC_START}, {"a", HID_SC_A}, {"b", HID_SC_B}, {"c", HID_SC_C},
    {"x", HID_SC_X}, {"y", HID_SC_Y}, {"z", HID_SC_Z}, {"mode", HID_SC_MODE},
  };
  *buttons = 0;
  for (;;) {
    size_t len = strcspn(s, "+");
    bool found = false;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]) && !found; i++) {
      if (strlen(names[i].name) == len && strncasecmp(s, names[i].name, len) == 0) {
        *buttons |= names[i].bit;
        found = true;
      }
    }
    if (!found) return false;
    if (!s[len]) return true;
    s += len + 1;
  }
}

bool kg_at_path_split(const char *path, char *dir, size_t dir_size, char *name, size_t name_size) {
  const char *slash = strrchr(path, '/');
  const char *file = slash ? slash + 1 : path;
  size_t dir_len = slash && slash != path ? (size_t) (slash - path) : 0;
  if (!*file || strlen(file) >= name_size || dir_len + 2 > dir_size) return false;
  if (dir_len) {
    memcpy(dir, path, dir_len);
    dir[dir_len] = '\0';
  } else {
    strcpy(dir, "/");
  }
  strcpy(name, file);
  return true;
}
