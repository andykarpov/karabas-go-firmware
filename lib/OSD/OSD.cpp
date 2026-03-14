/*
 Copyright (C) 2021 Andy Karpov <andy.karpov@gmail.com>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */

// STL headers
// C headers
// Framework headers
// Library headers
#include <SPI.h>
// Project headers
// This component's header
#include <OSD.h>
#include <Arduino.h>

/****************************************************************************/

// Commands

const uint8_t CMD_OSD = 0x20;

const uint8_t ADDR_SHOW = 0x01;
const uint8_t ADDR_POPUP = 0x02;

const uint8_t ADDR_CLEAR = 0x0F;
const uint8_t ADDR_SET_POS_X = 0x10;
const uint8_t ADDR_SET_POS_Y = 0x11;
const uint8_t ADDR_CHAR = 0x12;
const uint8_t ADDR_ATTR = 0x13;
const uint8_t ADDR_NOOP = 0x14;

const uint8_t ADDR_FONT_RST = 0x20;
const uint8_t ADDR_FONT_DATA = 0x21;
const uint8_t ADDR_FONT_DATA_WR = 0x22;

/****************************************************************************/

OSD::OSD(void)
{
}

/****************************************************************************/

void OSD::begin(m_cb act)
{
  action = act;
}

/****************************************************************************/

void OSD::setPos(uint8_t x, uint8_t y)
{
  current_x = x;
  current_y = y;

  if (x >= SIZE_X) {
    current_x = 0;
    current_y++;
  }

  if (y >= SIZE_Y) {
    current_y = 0;
    current_x = 0;
  }
}

/****************************************************************************/

void OSD::clear(void)
{
  setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  fill(OSD::CHAR_VOID);
}

void OSD::fill(uint8_t chr)
{
  setPos(0,0);
  for (uint8_t y=0; y<SIZE_Y; y++) {
    for (uint8_t x=0; x<SIZE_X; x++) {
      write(chr);
    }
  }
}

void OSD::fill(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t chr)
{
    setPos(x1, y1);
    for (uint8_t y=y1; y<=y2; y++) {
        setPos(x1, y);
        for (uint8_t x=x1; x<=x2; x++) {
            write(chr);
        }
    }
}

void OSD::frame(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t thickness)
{
    setPos(x1,y1);
    for(uint8_t y=y1; y<=y2; y++) {
        setPos(x1, y);
        for(uint8_t x=x1; x<=x2; x++) {
            if (y==y1 && x==x1) {
                write(OSD::CHAR_LT); // lt
            }
            else if (y==y2 && x==x1) {
                write(OSD::CHAR_LB); // lb
            }
            else if (y==y1 && x==x2) {
                write(OSD::CHAR_RT); // rt
            }
            else if (y==y2 && x==x2) {
                write(OSD::CHAR_RB); // rb
            }
            else if (y==y1 || y == y2) {
                write(OSD::CHAR_TB); // t / b
            }
            else if ((x==x1 && y>y1 && y<y2) || (x==x2 && y>y1 && y<y2)) {
                setPos(x,y);
                write(OSD::CHAR_LR); // l / r
            }
        }
    }
}

void OSD::progress(uint8_t x, uint8_t y, uint8_t size, uint32_t val, uint32_t max, uint8_t cl, uint8_t bg)
{
  setPos(x,y);
  // fill with 0xfe (small square)
  uint8_t v = map(val, 0, max, 0, size);
  for (uint8_t i=0; i<v; i++) {
    setColor(cl, bg);
    write(OSD::CHAR_SQUARE);
  }
  // fill empty space
  for (uint8_t i=v; i<size; i++) {
    setColor(OSD::COLOR_GREY, bg);
    write(OSD::CHAR_SQUARE);
  }
}

void OSD::loadingPopup(uint32_t val, uint32_t max)
{
  uint8_t bg = OSD::COLOR_BLUE;
  setColor(OSD::COLOR_WHITE, bg);
  frame(8,8,24,12, 1);
  setColor(OSD::COLOR_WHITE, bg);
  fill(9,9,23,11, OSD::CHAR_VOID);
  setColor(OSD::COLOR_YELLOW_I, bg);
  setPos(9, 9);
  print("Loading...");
  setColor(OSD::COLOR_CYAN_I, bg);
  setPos(9,10);
  print("Please wait.");
  setColor(OSD::COLOR_WHITE, bg);
  progress(9, 11, 15, val, max, OSD::COLOR_WHITE, bg);
}

void OSD::scrollbar(uint8_t x, uint8_t y, uint8_t height, uint32_t val, uint32_t max)
{
  uint8_t v = map(val, 0, max, 0, height);
  for (uint8_t i=0; i<height; i++) {
    setPos(x, y+i);
    write(i == v ? OSD::CHAR_SCROLL_HANDLE : OSD::CHAR_LR);
  }
}

/****************************************************************************/

size_t OSD::write(uint8_t chr)
{
  uint8_t prev_attr = attr[current_y][current_x];
  uint8_t prev_data = data[current_y][current_x];
  uint8_t color = fg_color << 4;
  data[current_y][current_x] = chr;
  attr[current_y][current_x] = bg_color + color;
  if (prev_attr != bg_color+color || prev_data != chr) {
    changed[current_y][current_x] = true;
  }
  setPos(current_x+1, current_y);
  return 1;
}

void OSD::update()
{
  for (uint8_t y=0; y<OSD::SIZE_Y; y++) {
    for (uint8_t x=0; x<OSD::SIZE_X; x++) {
      if (changed[y][x]) {
        action(CMD_OSD, ADDR_SET_POS_X, x);
        action(CMD_OSD, ADDR_SET_POS_Y, y);
        action(CMD_OSD, ADDR_CHAR, data[y][x]);
        action(CMD_OSD, ADDR_ATTR, attr[y][x]);
        changed[y][x] = false;
      }
    }
  }
}

/****************************************************************************/

void OSD::setColor(uint8_t color, uint8_t bgcolor)
{
    fg_color = color;
    bg_color = bgcolor;
}

/****************************************************************************/

void OSD::showMenu() {
  action(CMD_OSD, ADDR_SHOW, 1);
}

void OSD::hideMenu() {
  action(CMD_OSD, ADDR_SHOW, 0);
}

void OSD::showPopup() {
  action(CMD_OSD, ADDR_POPUP, 1);
}

void OSD::hidePopup() {
  action(CMD_OSD, ADDR_POPUP, 0);
}

void OSD::fontReset() {
  action(CMD_OSD, ADDR_FONT_RST, 1);
  action(CMD_OSD, ADDR_FONT_RST, 0);
}

void OSD::fontSend(uint8_t data) {
  action (CMD_OSD, ADDR_FONT_DATA, data);
  action (CMD_OSD, ADDR_FONT_DATA_WR, 1);
}

void OSD::line(uint8_t y) {
  setPos(0,y);
  for (uint8_t i=0; i<OSD::SIZE_X; i++) {
    write(OSD::CHAR_LINE); 
  }
}

void OSD::logo(uint8_t x, uint8_t y, uint8_t hw) {
  // karabas go logo
  uint8_t logo[56] = {
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb2, 0xb3, 0xb6, 0xb7, 0xb2, 0xb3, 0xb8, 0xb9,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc2, 0xc3, 0xc6, 0xc7, 0xc2, 0xc3, 0xc8, 0xc9,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xba, 0xbb, 0xbc, 0xbd,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xca, 0xcb, 0xcc, 0xcd 
  };

  setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  for (uint8_t j=0; j<4; j++) {
    for (uint8_t i=0; i<14; i++) {
      setPos(x+i, y+j);
      write(logo[i+14*j]);
    }
  }

  setPos(x+1, y+2);
  setColor(OSD::COLOR_RED_I, OSD::COLOR_BLACK); write(OSD::CHAR_BRICK);
  setColor(OSD::COLOR_YELLOW_I, OSD::COLOR_BLACK); write(OSD::CHAR_BRICK);
  setColor(OSD::COLOR_GREEN_I, OSD::COLOR_BLACK); write(OSD::CHAR_BRICK);
  setColor(OSD::COLOR_BLUE_I, OSD::COLOR_BLACK); write(OSD::CHAR_BRICK);

  if (hw == 2 || hw == 3) {
    setPos(x+6, y+3);
    setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
    print("mini");
  }

  setColor(OSD::COLOR_GREY, OSD::COLOR_BLACK);
  setPos(x,y+3);
}

void OSD::header(char* build, char* id, uint8_t hw)
{
  logo(0,0, hw);

  setColor(OSD::COLOR_GREY, OSD::COLOR_BLACK);
  setPos(19,2);
  print("FPGA "); print(build);

  setColor(OSD::COLOR_GREY, OSD::COLOR_BLACK);
  setPos(19,3);
  print("CORE "); print(id);

  setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  line(4);
}

void OSD::footer() {

  setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  line(22);

  // footer
  setPos(0,23); 
  print("Press ESC to return");
}


// vim:cin:ai:sts=2 sw=2 ft=cpp
