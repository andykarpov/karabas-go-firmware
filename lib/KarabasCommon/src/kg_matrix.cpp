#include "kg_matrix.h"

#include <stdio.h>
#include <string.h>

void kg_matrix_parse(const uint8_t *block, kg_matrix_cfg_t *out) {
  memset(out, 0, sizeof(*out));
  out->mode = block[0] < KG_MATRIX_MODE_COUNT ? block[0] : KG_MATRIX_MODE_CORE;
  out->color = block[1] < KG_MATRIX_COLOR_COUNT ? block[1] : KG_MATRIX_COLOR_DEFAULT;
  out->speed = block[2] == 0xFF ? 0 : block[2];
  out->repeat = block[3] == 0xFF ? 0 : block[3];
  out->then = (block[4] == KG_MATRIX_MODE_EFFECT || block[4] == KG_MATRIX_MODE_BLANK) ? block[4] : KG_MATRIX_MODE_CORE;
  out->effect = block[5] < KG_MATRIX_FX_COUNT ? block[5] : KG_MATRIX_FX_CLOCK;
  out->brightness = (block[6] >= 1 && block[6] <= 16) ? block[6] - 1 : KG_MATRIX_DEFAULT_BRIGHTNESS;
  out->frame_count = (block[7] >= 1 && block[7] <= KG_MATRIX_FRAMES) ? block[7] : 1;

  for (uint8_t i = 0; i < KG_MATRIX_TEXT_LEN; i++) {
    uint8_t c = block[KG_MATRIX_POS_TEXT + i];
    if (c == 0 || c == 0xFF) break;
    out->text[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
  }
  memcpy(out->frames, block + KG_MATRIX_POS_FRAMES, sizeof(out->frames));

  // nothing to show: back to the core
  if (out->mode == KG_MATRIX_MODE_SCROLL && out->text[0] == '\0') out->mode = KG_MATRIX_MODE_CORE;
}

uint8_t kg_matrix_frame_pixel(const uint8_t *frame, uint8_t x, uint8_t y) {
  if (x >= KG_MATRIX_W || y >= KG_MATRIX_H) return KG_MX_OFF;
  uint8_t byte = y * 2 + x / 8;
  uint8_t mask = 0x80 >> (x % 8);
  uint8_t red = (frame[byte] & mask) ? KG_MX_RED : 0;
  uint8_t green = (frame[16 + byte] & mask) ? KG_MX_GREEN : 0;
  return red | green;
}

uint8_t kg_matrix_text_color(uint8_t color, int x, int ch) {
  static const uint8_t rainbow[3] = {KG_MX_RED, KG_MX_YELLOW, KG_MX_GREEN};
  switch (color) {
    case KG_MATRIX_COLOR_RED: return KG_MX_RED;
    case KG_MATRIX_COLOR_GREEN: return KG_MX_GREEN;
    case KG_MATRIX_COLOR_RAINBOW: return rainbow[(ch < 0 ? -ch : ch) % 3];
    case KG_MATRIX_COLOR_GRADIENT: return x < 6 ? KG_MX_GREEN : x < 11 ? KG_MX_YELLOW : KG_MX_RED;
    default: return KG_MX_YELLOW;
  }
}

size_t kg_matrix_expand(const char *text, const kg_matrix_vars_t *vars, char *out, size_t size) {
  if (size == 0) return 0;
  size_t len = 0;
  char buf[16];
  while (*text) {
    const char *value = nullptr;
    size_t skip = 0;
    if (*text == '{') {
      if (strncmp(text, "{name}", 6) == 0) { value = vars->name; skip = 6; }
      else if (strncmp(text, "{build}", 7) == 0) { value = vars->build; skip = 7; }
      else if (strncmp(text, "{time}", 6) == 0) {
        snprintf(buf, sizeof(buf), "%02u:%02u", vars->hour % 100, vars->minute % 100);
        value = buf; skip = 6;
      }
      else if (strncmp(text, "{date}", 6) == 0) {
        snprintf(buf, sizeof(buf), "%02u.%02u.%04u", vars->day % 100, vars->month % 100, vars->year % 10000);
        value = buf; skip = 6;
      }
    }
    if (skip) {
      for (const char *v = value ? value : ""; *v && len < size - 1; v++) out[len++] = *v;
      text += skip;
    } else {
      if (len < size - 1) out[len++] = *text;
      text++;
    }
  }
  out[len] = '\0';
  return len;
}
