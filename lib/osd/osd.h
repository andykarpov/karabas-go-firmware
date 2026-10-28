/*
 Copyright (C) 2021 Andy Karpov <andy.karpov@gmail.com>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */
#pragma once

// The text OSD of the FPGA core (kg_osd_overlay): 32 x 26 characters with attributes. Drawing
// goes into a frame buffer; osd_update() sends the changed cells to the core (CMD_OSD frames
// through the callback). Pure C.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OSD_SIZE_X 32
#define OSD_SIZE_Y 26

// colors: bit 3 red, bit 2 green, bit 1 blue, bit 0 bright (flash on black)
#define OSD_COLOR_BLACK     0x0
#define OSD_COLOR_FLASH     0x1
#define OSD_COLOR_WHITE     0xF
#define OSD_COLOR_GREY      0xE
#define OSD_COLOR_RED       0x8
#define OSD_COLOR_GREEN     0x4
#define OSD_COLOR_BLUE      0x2
#define OSD_COLOR_RED_I     0x9
#define OSD_COLOR_GREEN_I   0x5
#define OSD_COLOR_BLUE_I    0x3
#define OSD_COLOR_YELLOW    0xC
#define OSD_COLOR_YELLOW_I  0xD
#define OSD_COLOR_MAGENTA   0xA
#define OSD_COLOR_MAGENTA_I 0xB
#define OSD_COLOR_CYAN      0x6
#define OSD_COLOR_CYAN_I    0x7

// characters of the OSD font
#define OSD_CHAR_VOID 0
#define OSD_CHAR_LT 210
#define OSD_CHAR_RT 211
#define OSD_CHAR_LB 212
#define OSD_CHAR_RB 213
#define OSD_CHAR_LR 208
#define OSD_CHAR_TB 209
#define OSD_CHAR_SQUARE 254
#define OSD_CHAR_SMALL_SQUARE 249
#define OSD_CHAR_LINE 209
#define OSD_CHAR_BRICK 22
#define OSD_CHAR_ARROW_UP 30
#define OSD_CHAR_ARROW_DOWN 31
#define OSD_CHAR_ARROW_LEFT 17
#define OSD_CHAR_ARROW_RIGHT 16
#define OSD_CHAR_FILLED_CELL 219
#define OSD_CHAR_HALF_CELL 221
#define OSD_CHAR_SCROLL_HANDLE 220
#define OSD_CHAR_COPYRIGHT 127

typedef void (*osd_send_t)(uint8_t cmd, uint8_t addr, uint8_t data);

// hooks of the firmware: osd_update() holds the bus for all its frames
void osd_bus_lock(void);
void osd_bus_unlock(void);

void osd_begin(osd_send_t send);
void osd_set_pos(uint8_t x, uint8_t y);
void osd_set_color(uint8_t color, uint8_t bgcolor);
void osd_clear(void);
void osd_putc(uint8_t chr);               // at the position, which moves on
void osd_print(const char *s);
void osd_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void osd_update(void);                    // the changed cells to the core
void osd_fill_all(uint8_t chr);
void osd_fill(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t chr);
void osd_frame(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);
void osd_line(uint8_t y);
void osd_progress(uint8_t x, uint8_t y, uint8_t size, uint32_t val, uint32_t max, uint8_t cl, uint8_t bg);
void osd_scrollbar(uint8_t x, uint8_t y, uint8_t height, uint32_t val, uint32_t max);
void osd_logo(uint8_t x, uint8_t y, uint8_t hw);
void osd_header(const char *build, const char *id, uint8_t hw);
void osd_footer(void);
// a box with a title (up to 15 chars), "Please wait." and a progress bar
void osd_progress_popup(const char *title, uint32_t val, uint32_t max);
void osd_loading_popup(uint32_t val, uint32_t max); // "Loading..."
void osd_show_menu(void);
void osd_hide_menu(void);
void osd_show_popup(uint8_t i);
void osd_hide_popup(void);
void osd_font_reset(void);
void osd_font_send(uint8_t data);

#ifdef __cplusplus
}
#endif
