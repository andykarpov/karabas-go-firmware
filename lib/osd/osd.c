/*
 Copyright (C) 2021 Andy Karpov <andy.karpov@gmail.com>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */

#include "osd.h"
#include <stdarg.h>
#include <stdio.h>

#define CMD_OSD 0x20
#define ADDR_SHOW 0x01
#define ADDR_POPUP 0x02
#define ADDR_SET_POS_X 0x10
#define ADDR_SET_POS_Y 0x11
#define ADDR_CHAR 0x12
#define ADDR_ATTR 0x13
#define ADDR_FONT_RST 0x20
#define ADDR_FONT_DATA 0x21
#define ADDR_FONT_DATA_WR 0x22

static osd_send_t send;
static uint8_t fg_color, bg_color;
static uint8_t current_x, current_y;
static uint8_t data[OSD_SIZE_Y][OSD_SIZE_X];
static uint8_t attr[OSD_SIZE_Y][OSD_SIZE_X];
static bool changed[OSD_SIZE_Y][OSD_SIZE_X];

void osd_begin(osd_send_t s) {
  send = s;
}

void osd_set_pos(uint8_t x, uint8_t y) {
  current_x = x;
  current_y = y;
  if (x >= OSD_SIZE_X) {
    current_x = 0;
    current_y++;
  }
  if (current_y >= OSD_SIZE_Y) { // also the wrap past the end of the last row
    current_y = 0;
    current_x = 0;
  }
}

void osd_set_color(uint8_t color, uint8_t bgcolor) {
  fg_color = color;
  bg_color = bgcolor;
}

void osd_putc(uint8_t chr) {
  uint8_t a = bg_color + (fg_color << 4);
  if (attr[current_y][current_x] != a || data[current_y][current_x] != chr) {
    changed[current_y][current_x] = true;
  }
  data[current_y][current_x] = chr;
  attr[current_y][current_x] = a;
  osd_set_pos(current_x + 1, current_y);
}

void osd_print(const char *s) {
  while (*s) osd_putc((uint8_t) *s++);
}

void osd_printf(const char *fmt, ...) {
  char buf[OSD_SIZE_X * 2 + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  osd_print(buf);
}

void osd_update(void) {
  osd_bus_lock();
  for (uint8_t y = 0; y < OSD_SIZE_Y; y++) {
    for (uint8_t x = 0; x < OSD_SIZE_X; x++) {
      if (changed[y][x]) {
        send(CMD_OSD, ADDR_SET_POS_X, x);
        send(CMD_OSD, ADDR_SET_POS_Y, y);
        send(CMD_OSD, ADDR_CHAR, data[y][x]);
        send(CMD_OSD, ADDR_ATTR, attr[y][x]);
        changed[y][x] = false;
      }
    }
  }
  osd_bus_unlock();
}

void osd_clear(void) {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_fill_all(OSD_CHAR_VOID);
}

void osd_fill_all(uint8_t chr) {
  osd_set_pos(0, 0);
  for (uint16_t i = 0; i < OSD_SIZE_X * OSD_SIZE_Y; i++) osd_putc(chr);
}

void osd_fill(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t chr) {
  for (uint8_t y = y1; y <= y2; y++) {
    osd_set_pos(x1, y);
    for (uint8_t x = x1; x <= x2; x++) osd_putc(chr);
  }
}

void osd_frame(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2) {
  for (uint8_t y = y1; y <= y2; y++) {
    osd_set_pos(x1, y);
    for (uint8_t x = x1; x <= x2; x++) {
      if (y == y1 && x == x1) osd_putc(OSD_CHAR_LT);
      else if (y == y2 && x == x1) osd_putc(OSD_CHAR_LB);
      else if (y == y1 && x == x2) osd_putc(OSD_CHAR_RT);
      else if (y == y2 && x == x2) osd_putc(OSD_CHAR_RB);
      else if (y == y1 || y == y2) osd_putc(OSD_CHAR_TB);
      else if (x == x1 || x == x2) {
        osd_set_pos(x, y);
        osd_putc(OSD_CHAR_LR);
      }
    }
  }
}

void osd_line(uint8_t y) {
  osd_set_pos(0, y);
  for (uint8_t i = 0; i < OSD_SIZE_X; i++) osd_putc(OSD_CHAR_LINE);
}

// val 0..max -> 0..size (Arduino's map())
static uint8_t scale(uint32_t val, uint32_t max, uint8_t size) {
  return max ? (uint8_t) ((uint64_t) val * size / max) : 0;
}

void osd_progress(uint8_t x, uint8_t y, uint8_t size, uint32_t val, uint32_t max, uint8_t cl, uint8_t bg) {
  osd_set_pos(x, y);
  uint8_t v = scale(val, max, size);
  for (uint8_t i = 0; i < size; i++) {
    osd_set_color(i < v ? cl : OSD_COLOR_GREY, bg);
    osd_putc(OSD_CHAR_SQUARE);
  }
}

void osd_scrollbar(uint8_t x, uint8_t y, uint8_t height, uint32_t val, uint32_t max) {
  uint8_t v = scale(val, max, height);
  for (uint8_t i = 0; i < height; i++) {
    osd_set_pos(x, y + i);
    osd_putc(i == v ? OSD_CHAR_SCROLL_HANDLE : OSD_CHAR_LR);
  }
}

void osd_loading_popup(uint32_t val, uint32_t max) {
  uint8_t bg = OSD_COLOR_BLUE;
  osd_set_color(OSD_COLOR_WHITE, bg);
  osd_frame(8, 8, 24, 12);
  osd_fill(9, 9, 23, 11, OSD_CHAR_VOID);
  osd_set_color(OSD_COLOR_YELLOW_I, bg);
  osd_set_pos(9, 9);
  osd_print("Loading...");
  osd_set_color(OSD_COLOR_CYAN_I, bg);
  osd_set_pos(9, 10);
  osd_print("Please wait.");
  osd_set_color(OSD_COLOR_WHITE, bg);
  osd_progress(9, 11, 15, val, max, OSD_COLOR_WHITE, bg);
}

void osd_logo(uint8_t x, uint8_t y, uint8_t hw) {
  // karabas go logo
  static const uint8_t logo[56] = {
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb2, 0xb3, 0xb6, 0xb7, 0xb2, 0xb3, 0xb8, 0xb9,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc2, 0xc3, 0xc6, 0xc7, 0xc2, 0xc3, 0xc8, 0xc9,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xba, 0xbb, 0xbc, 0xbd,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xca, 0xcb, 0xcc, 0xcd
  };
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  for (uint8_t j = 0; j < 4; j++) {
    for (uint8_t i = 0; i < 14; i++) {
      osd_set_pos(x + i, y + j);
      osd_putc(logo[i + 14 * j]);
    }
  }
  osd_set_pos(x + 1, y + 2);
  osd_set_color(OSD_COLOR_RED_I, OSD_COLOR_BLACK); osd_putc(OSD_CHAR_BRICK);
  osd_set_color(OSD_COLOR_YELLOW_I, OSD_COLOR_BLACK); osd_putc(OSD_CHAR_BRICK);
  osd_set_color(OSD_COLOR_GREEN_I, OSD_COLOR_BLACK); osd_putc(OSD_CHAR_BRICK);
  osd_set_color(OSD_COLOR_BLUE_I, OSD_COLOR_BLACK); osd_putc(OSD_CHAR_BRICK);
  if (hw == 2 || hw == 3) {
    osd_set_pos(x + 6, y + 3);
    osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
    osd_print("mini");
  }
  osd_set_color(OSD_COLOR_GREY, OSD_COLOR_BLACK);
  osd_set_pos(x, y + 3);
}

void osd_header(const char *build, const char *id, uint8_t hw) {
  osd_logo(0, 0, hw);
  osd_set_color(OSD_COLOR_GREY, OSD_COLOR_BLACK);
  osd_set_pos(19, 2);
  osd_print("FPGA "); osd_print(build);
  osd_set_pos(19, 3);
  osd_print("CORE "); osd_print(id);
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_line(4);
}

void osd_footer(void) {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_line(22);
  osd_set_pos(0, 23);
  osd_print("Press ESC to return");
}

void osd_show_menu(void) { send(CMD_OSD, ADDR_SHOW, 1); }
void osd_hide_menu(void) { send(CMD_OSD, ADDR_SHOW, 0); }
void osd_show_popup(uint8_t i) { send(CMD_OSD, ADDR_POPUP, (i << 4) + 1); }
void osd_hide_popup(void) { send(CMD_OSD, ADDR_POPUP, 0); }

void osd_font_reset(void) {
  send(CMD_OSD, ADDR_FONT_RST, 1);
  send(CMD_OSD, ADDR_FONT_RST, 0);
}

void osd_font_send(uint8_t d) {
  send(CMD_OSD, ADDR_FONT_DATA, d);
  send(CMD_OSD, ADDR_FONT_DATA_WR, 1);
}
