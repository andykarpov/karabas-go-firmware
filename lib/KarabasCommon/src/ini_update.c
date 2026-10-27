#include "ini_update.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static bool is_space(char c) { return c == ' ' || c == '\t'; }

static const char *skip_space(const char *s) {
  while (is_space(*s)) s++;
  return s;
}

// s[0..len) with the trailing spaces dropped equals name, ignoring case
static bool name_eq(const char *s, size_t len, const char *name) {
  while (len > 0 && is_space(s[len - 1])) len--;
  if (strlen(name) != len) return false;
  for (size_t i = 0; i < len; i++) {
    if (tolower((unsigned char) s[i]) != tolower((unsigned char) name[i])) return false;
  }
  return true;
}

static bool section_eq(const char *a, const char *b) { return name_eq(a, strlen(a), b); }

static void emit(kg_ini_update_t *u, const char *line) {
  u->out(u->ctx, line);
  u->any = true;
}

static void emit_blanks(kg_ini_update_t *u) {
  for (; u->blanks > 0; u->blanks--) emit(u, "");
}

static void emit_item(kg_ini_update_t *u, kg_ini_item_t *it) {
  char line[KG_INI_LINE_MAX + 1];
  snprintf(line, sizeof(line), "%s=%s", it->key, it->value);
  emit(u, line);
  it->done = true;
}

// the keys of the current section that are not in the file yet
static void flush_section(kg_ini_update_t *u) {
  if (!u->section[0]) return;
  for (uint8_t i = 0; i < u->count; i++) {
    kg_ini_item_t *it = &u->items[i];
    if (!it->done && section_eq(it->section, u->section)) emit_item(u, it);
  }
}

void kg_ini_update_begin(kg_ini_update_t *u, kg_ini_item_t *items, uint8_t count, kg_ini_out_t out, void *ctx) {
  memset(u, 0, sizeof(*u));
  u->items = items;
  u->count = count;
  u->out = out;
  u->ctx = ctx;
  for (uint8_t i = 0; i < count; i++) items[i].done = false;
}

void kg_ini_update_line(kg_ini_update_t *u, const char *line) {
  const char *p = skip_space(line);
  if (!*p) {
    u->blanks++;
    return;
  }

  if (*p == '[') {
    const char *name = skip_space(p + 1);
    const char *end = strchr(name, ']');
    if (end) {
      flush_section(u);
      size_t len = end - name;
      while (len > 0 && is_space(name[len - 1])) len--;
      if (len > KG_INI_NAME_MAX) len = KG_INI_NAME_MAX;
      memcpy(u->section, name, len);
      u->section[len] = '\0';
    }
    emit_blanks(u);
    emit(u, line);
    return;
  }

  emit_blanks(u);
  const char *eq = (*p == ';' || *p == '#') ? NULL : strchr(p, '=');
  if (eq && u->section[0]) {
    for (uint8_t i = 0; i < u->count; i++) {
      kg_ini_item_t *it = &u->items[i];
      if (section_eq(it->section, u->section) && name_eq(p, eq - p, it->key)) {
        emit_item(u, it);
        return;
      }
    }
  }
  emit(u, line);
}

void kg_ini_update_end(kg_ini_update_t *u) {
  flush_section(u);
  bool blank = u->blanks > 0; // the file ends with a blank line: no other one before a new section
  emit_blanks(u);
  for (uint8_t i = 0; i < u->count; i++) {
    if (u->items[i].done) continue;
    const char *section = u->items[i].section;
    if (u->any && !blank) emit(u, "");
    blank = false;
    char line[KG_INI_LINE_MAX + 1];
    snprintf(line, sizeof(line), "[%s]", section);
    emit(u, line);
    for (uint8_t j = i; j < u->count; j++) {
      kg_ini_item_t *it = &u->items[j];
      if (!it->done && section_eq(it->section, section)) emit_item(u, it);
    }
  }
}
