#pragma once

#include <Arduino.h>
#include "types.h"

// A page of the graphic setup (app_ft_setup) that is more than a list of settings: Clock, Wi-Fi,
// Gamepad. The setup draws the tabs, the title and the footer; the page draws its panel. Enter
// on its tab gives the page the keys (open), it gives them back by returning false from key or
// click (close follows). While its tab is only chosen, the page is drawn without the focus.
// The changes of a page are done at once (the RTC, the ESP8266, a driver ini), not by Save.

typedef struct {
  const setup_t *s;  // the colors of the edited setup
  int16_t x, y, w, h; // the panel under the title line
  uint16_t opt;      // FT81x_OPT_3D or FT81x_OPT_FLAT
  bool focused;      // the page has the keys
} ft_page_ctx_t;

typedef struct {
  void (*open)();
  void (*close)();
  void (*draw)(const ft_page_ctx_t &c); // adds its clickable areas with ft_page_hit() while focused
  // a pressed key (joy: from a joystick, as the arrows, Enter and Esc); false: back to the tabs
  bool (*key)(uint8_t key, bool shift, bool joy);
  bool (*tick)();                       // while focused; true: redraw
  bool (*click)(int16_t id);            // an area of ft_page_hit(); false: back to the tabs
  void (*wheel)(int16_t id, int d);     // id is FT_HIT_NONE outside the areas; d > 0: up
  const char *(*hint)();
  const char *(*keys)();
} ft_page_t;

// the ids of the page areas are 0..999
#define FT_PAGE_HIT 1000

bool ft_page_hit(int16_t x, int16_t y, int16_t w, int16_t h, int16_t id);
// a button: the active color when focused, a frame when hovered (the setup's tab look)
void ft_page_button(const ft_page_ctx_t &c, int16_t x, int16_t y, int16_t w, const char *label, bool focused, int16_t id);
// a row that can be picked (lists, fields): a band when focused or hovered
void ft_page_row(const ft_page_ctx_t &c, int16_t x, int16_t y, int16_t w, int16_t h, bool focused, bool hovered);
// text in a font of the FT812, the chars it has no glyph for as '?'
void ft_page_text(int16_t x, int16_t y, uint8_t font, uint32_t color, uint16_t opt, const char *text, uint8_t max = 40);

#define FT_PAGE_BTN_H 32
#define FT_PAGE_FONT 28
#define FT_PAGE_FONT_SMALL 27
#define FT_PAGE_ERROR 0xff4040
#define FT_PAGE_OK 0x40d040
