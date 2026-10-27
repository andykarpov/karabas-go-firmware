#pragma once

#include <Arduino.h>

// The mouse pointer of the graphic menus (FT812, APP only).
// A screen ends its frame with ft_frame_end() instead of ft_swap_screen(): the frame is kept in
// GRAM, so a move of the pointer only appends the kept frame and draws the pointer at its new
// place (ft_pointer_frame), a few bytes over SPI. While drawing, the screen adds its clickable
// areas with ft_hit_add(); ft_hover() is the one under the pointer. The pointer hides after a
// few seconds without a move and on a key.

#define FT_HIT_NONE -1

typedef struct {
  bool moved;      // the pointer moved, showed or hid: redraw it
  uint8_t pressed; // buttons pressed since the last poll (bit 0 left, bit 1 right)
  int8_t wheel;    // > 0: up
  uint8_t buttons; // held now (a drag)
} ft_mouse_event_t;

#define FT_MOUSE_LEFT 1
#define FT_MOUSE_RIGHT 2

void ft_mouse_load();  // the pointer bitmap into GRAM (with the other images, after ft.init)
void ft_mouse_reset(); // drops the moves made meanwhile, hides the pointer
// true if there is something to do; a click on a hidden pointer only shows it
bool ft_mouse_poll(ft_mouse_event_t *ev);
void ft_mouse_hide();
int16_t ft_mouse_x();
int16_t ft_mouse_y();

void ft_hit_clear();
// true if the pointer is over it (draw it hovered)
bool ft_hit_add(int16_t x, int16_t y, int16_t w, int16_t h, int16_t id);
int16_t ft_hit_at(int16_t x, int16_t y);
int16_t ft_hover(); // the area under the pointer, FT_HIT_NONE while it is hidden

void ft_frame_end();
void ft_pointer_frame();
