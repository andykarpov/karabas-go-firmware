#include <Arduino.h>
#include "main.h"
#include "FT812.h"
#include "ui/ft_mouse.h"
#include "ui/ft_page.h"

bool ft_page_hit(int16_t x, int16_t y, int16_t w, int16_t h, int16_t id) {
  return ft_hit_add(x, y, w, h, FT_PAGE_HIT + id);
}

void ft_page_button(const ft_page_ctx_t &c, int16_t x, int16_t y, int16_t w, const char *label, bool focused, int16_t id) {
  bool lit = focused && c.focused;
  if (c.focused && ft_page_hit(x, y, w, FT_PAGE_BTN_H, id)) {
    ft.drawRect(x, y, w, FT_PAGE_BTN_H, 2, c.s->color_active); // hovered: a 2 px frame
  }
  ft.drawButton(x, y, w, FT_PAGE_BTN_H, FT_PAGE_FONT, lit ? c.s->color_text_active : c.s->color_text,
    lit ? c.s->color_active : c.s->color_button, c.opt, label);
}

void ft_page_row(const ft_page_ctx_t &c, int16_t x, int16_t y, int16_t w, int16_t h, bool focused, bool hovered) {
  if (!c.focused) return;
  if (focused || hovered) ft.drawRect(x, y, w, h, 4, c.s->color_button);
  if (focused) ft.drawRect(x, y + 4, 4, h - 8, 1, c.s->color_active);
}

void ft_page_text(int16_t x, int16_t y, uint8_t font, uint32_t color, uint16_t opt, const char *text, uint8_t max) {
  char b[64];
  uint8_t n = 0;
  if (max >= sizeof(b)) max = sizeof(b) - 1;
  for (; text[n] && n < max; n++) b[n] = (text[n] >= 32 && text[n] < 127) ? text[n] : '?';
  b[n] = '\0';
  ft.drawText(x, y, font, color, opt, b);
}
