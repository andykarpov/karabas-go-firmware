#include "matrix_style.h"
#include "kg_matrix.h"
#include <ctype.h>
#include <stddef.h>

static const char *const colors[] = { "Green", "Yellow", "Red" };
static const char *const seconds[] = { "Dot", "Colon", "Bar", "Off" };
static const char *const styles[] = { "Bars", "Wide", "Center", "Mono", "Vertical" };
static const char *const meter_colors[] = { "Zones", "Green", "Yellow", "Red", "Fire" };
static const char *const decays[] = { "Slow", "Normal", "Fast" };

typedef struct {
  const char *key;
  const char *const *names;
  uint8_t count;
  uint8_t def;
} opt_t;

#define OPT(key, names, def) { key, names, sizeof(names) / sizeof(names[0]), def }
static const opt_t opts[KG_MX_OPT_COUNT] = {
  OPT("clock_hours", colors, KG_MX_CLR_GREEN),
  OPT("clock_minutes", colors, KG_MX_CLR_YELLOW),
  OPT("clock_seconds", seconds, KG_MX_SEC_DOT),
  OPT("clock_seconds_color", colors, KG_MX_CLR_RED),
  OPT("meter_style", styles, KG_MX_METER_BARS),
  OPT("meter_colors", meter_colors, KG_MX_METER_COLORS_ZONES),
  OPT("meter_decay", decays, KG_MX_DECAY_NORMAL),
};

void kg_mx_style_default(kg_mx_style_t *s) {
  for (uint8_t i = 0; i < KG_MX_OPT_COUNT; i++) s->opt[i] = opts[i].def;
  s->meter_peak = false;
}

uint8_t kg_mx_opt_count(uint8_t opt) { return opt < KG_MX_OPT_COUNT ? opts[opt].count : 0; }

const char *kg_mx_opt_name(uint8_t opt, uint8_t v) {
  return opt < KG_MX_OPT_COUNT && v < opts[opt].count ? opts[opt].names[v] : "?";
}

const char *kg_mx_opt_key(uint8_t opt) { return opt < KG_MX_OPT_COUNT ? opts[opt].key : ""; }

static bool same(const char *a, const char *b) {
  while (*a && *b && tolower((unsigned char) *a) == tolower((unsigned char) *b)) { a++; b++; }
  return *a == '\0' && *b == '\0';
}

int kg_mx_opt_parse(uint8_t opt, const char *s) {
  if (opt >= KG_MX_OPT_COUNT || !s) return -1;
  for (uint8_t v = 0; v < opts[opt].count; v++) {
    if (same(s, opts[opt].names[v])) return v;
  }
  return -1;
}

uint8_t kg_mx_color(uint8_t clr) {
  return clr == KG_MX_CLR_YELLOW ? KG_MX_YELLOW : clr == KG_MX_CLR_RED ? KG_MX_RED : KG_MX_GREEN;
}
