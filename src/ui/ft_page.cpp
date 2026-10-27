#include <Arduino.h>
#include "main.h"
#include "ft812.h"
#include "ui/ft_mouse.h"
#include "ui/ft_page.h"

bool ft_page_hit(int16_t x, int16_t y, int16_t w, int16_t h, int16_t id) {
  return ft_hit_add(x, y, w, h, FT_PAGE_HIT + id);
}

void ft_page_button(const ft_page_ctx_t &c, int16_t x, int16_t y, int16_t w, const char *label, bool focused, int16_t id) {
  bool lit = focused && c.focused;
  if (c.focused && ft_page_hit(x, y, w, FT_PAGE_BTN_H, id)) {
    ft_draw_rect(x, y, w, FT_PAGE_BTN_H, 2, c.s->color_active); // hovered: a 2 px frame
  }
  ft_draw_button(x, y, w, FT_PAGE_BTN_H, FT_PAGE_FONT, lit ? c.s->color_text_active : c.s->color_text,
    lit ? c.s->color_active : c.s->color_button, c.opt, label);
}

void ft_page_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t r, uint32_t color) {
  ft_draw_rect(x + r, y + r, w - 2 * r, h - 2 * r, r, color);
}

void ft_page_row(const ft_page_ctx_t &c, int16_t x, int16_t y, int16_t w, int16_t h, bool focused, bool hovered) {
  if (!c.focused) return;
  if (focused || hovered) ft_draw_rect(x, y, w, h, 4, c.s->color_button);
  if (focused) ft_page_marker(x, y, h, c.s->color_active, c.opt);
}

// a channel of the color scaled by num / 256, plus add (the light of the 3D dot)
static uint32_t shade(uint32_t color, uint16_t num, uint8_t add) {
  uint32_t r = 0;
  for (uint8_t sh = 0; sh <= 16; sh += 8) {
    uint16_t ch = (((color >> sh) & 0xff) * num >> 8) + add;
    r |= (uint32_t)(ch > 0xff ? 0xff : ch) << sh;
  }
  return r;
}

void ft_page_marker(int16_t x, int16_t y, int16_t h, uint32_t color, uint16_t opt) {
  int16_t cx = x + 9, cy = y + h / 2;
  if (opt & FT81x_OPT_FLAT) {
    ft_draw_circle(cx, cy, 3, color);
    return;
  }
  ft_draw_circle(cx + 1, cy + 1, 4, shade(color, 96, 0)); // the shadow
  ft_draw_circle(cx, cy, 3, color);
  ft_draw_circle(cx - 1, cy - 1, 1, shade(color, 128, 128)); // the light
}

void ft_page_text(int16_t x, int16_t y, uint8_t font, uint32_t color, uint16_t opt, const char *text, uint8_t max) {
  char b[64];
  uint8_t n = 0;
  if (max >= sizeof(b)) max = sizeof(b) - 1;
  for (; text[n] && n < max; n++) b[n] = (text[n] >= 32 && text[n] < 127) ? text[n] : '?';
  b[n] = '\0';
  ft_draw_text(x, y, font, color, opt, b);
}
