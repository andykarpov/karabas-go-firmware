/*
 Copyright (C) 2024 Andy Karpov <andy.karpov@gmail.com>

 Based on https://github.com/blazer82/FT81x_Arduino_Driver
 Copyright (c) 2020 Raphael Stäbler

 MIT License

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 */

#include <SPI.h>
#include <FT812.h>
#include <Arduino.h>

/****************************************************************************/

#define READ  0x000000  ///< Bitmask address reading
#define WRITE 0x800000  ///< Bitmask for address writing

#define DLSTART()                    0xFFFFFF00                                                                                                                             ///< Start display list
#define SWAP()                       0xFFFFFF01                                                                                                                             ///< Swap the current display list
#define MEMWRITE()                   0xFFFFFF1A                                                                                                                             ///< Write bytes into memory
#define CLEAR(c, s, t)               ((0x26L << 24) | ((c) << 2) | ((s) << 1) | (t))                                                                                        ///< Clear command
#define BEGIN(p)                     ((0x1FL << 24) | (p))                                                                                                                  ///< Begin primitive drawing
#define END()                        (0x21L << 24)                                                                                                                          ///< End primitive drawing
#define END_DL()                     0x00                                                                                                                                   ///< End current display list
#define CLEAR_COLOR_RGB(r, g, b)     ((0x02L << 24) | ((r) << 16) | ((g) << 8) | (b))                                                                                       ///< Clear with RGB color
#define CLEAR_COLOR(rgb)             ((0x02L << 24) | ((rgb)&0xFFFFFF))                                                                                                     ///< Clear with color
#define COLOR(rgb)                   ((0x04L << 24) | ((rgb)&0xFFFFFF))                                                                                                     ///< Create color
#define POINT_SIZE(s)                ((0x0DL << 24) | ((s)&0xFFF))                                                                                                          ///< Point size
#define LINE_WIDTH(w)                ((0x0EL << 24) | ((w)&0xFFF))                                                                                                          ///< Line width
#define VERTEX2II(x, y, h, c)        ((1L << 31) | (((uint32_t)(x)&0xFFF) << 21) | (((uint32_t)(y)&0xFFF) << 12) | ((uint32_t)(h) << 7) | (c))                              ///< Start the operation of graphics primitive at the specified coordinates in pixel precision.
#define VERTEX2F(x, y)               ((1L << 30) | (((uint32_t)(x)&0xFFFF) << 15) | ((uint32_t)(y)&0xFFFF))                                                                 ///< Start the operation of graphics primitives at the specified screen coordinate, in the pixel precision defined by VERTEX_FORMAT.
#define BITMAP_SOURCE(a)             ((1L << 24) | (a))                                                                                                                     ///< Specify the source address of bitmap data in FT81X graphics memory RAM_G.
#define BITMAP_LAYOUT(f, s, h)       ((7L << 24) | ((uint32_t)(f) << 19) | (((uint32_t)(s)&0x1FF) << 9) | ((uint32_t)(h)&0x1FF))                                            ///< Specify the source bitmap memory format and layout for the current handle.
#define BITMAP_SIZE(f, wx, wy, w, h) ((8L << 24) | ((uint32_t)((f)&1) << 20) | ((uint32_t)((wx)&1) << 19) | ((uint32_t)((wy)&1) << 18) | (((w)&0x1FF) << 9) | ((h)&0x1FF))  ///< Specify the screen drawing of bitmaps for the current handle
#define BGCOLOR()                    0xFFFFFF09                                                                                                                             ///< Set background color
#define FGCOLOR()                    0xFFFFFF0A                                                                                                                             ///< Set foreground color
#define PROGRESSBAR()                0xFFFFFF0F                                                                                                                             ///< Draw progressbar
#define SCROLLBAR()                  0xFFFFFF11                                                                                                                             ///< Draw scrollbar
#define LOADIDENTITY()               0xFFFFFF26                                                                                                                             ///< Set the current matrix to identity
#define SETMATRIX()                  0xFFFFFF2A                                                                                                                             ///< Write the current matrix as a bitmap transform
#define SCALE()                      0xFFFFFF28                                                                                                                             ///< Apply a scale to the current matrix
#define GRADIENT()                   0xFFFFFF0B                                                                                                                             ///< Draw gradient
#define TEXT()                       0xFFFFFF0C                                                                                                                             ///< Draw text
#define BUTTON()                     0xFFFFFF0D                                                                                                                             ///< Draw button
#define GAUGE()                      0xFFFFFF13                                                                                                                             ///< Draw gauge
#define CLOCK()                      0xFFFFFF14                                                                                                                             ///< Draw clock
#define SPINNER()                    0xFFFFFF16                                                                                                                             ///< Draw spinner
#define LOADIMAGE()                  0xFFFFFF24                                                                                                                             ///< Load image data
#define MEDIAFIFO()                  0xFFFFFF39                                                                                                                             ///< Set up media FIFO in general purpose graphics RAM
#define ROMFONT()                    0xFFFFFF3F   
#define LOGO()                       0xffffff31                                                                                                                          ///< Load a ROM font into bitmap handle

// KTOME addition
#define BLEND_FUNC(src, dst) ((0x0BL << 24) | (src << 3) | (dst))
#define CMD_ROTATE()         0xffffff29
#define CMD_TRANSLATE()      0xffffff27

#define BITMAPS      1  ///< Bitmap drawing primitive
#define POINTS       2  ///< Point drawing primitive
#define LINES        3  ///< Line drawing primitive
#define LINE_STRIP   4  ///< Line strip drawing primitive
#define EDGE_STRIP_R 5  ///< Edge strip right side drawing primitive
#define EDGE_STRIP_L 6  ///< Edge strip left side drawing primitive
#define EDGE_STRIP_A 7  ///< Edge strip above drawing primitive
#define EDGE_STRIP_B 8  ///< Edge strip below side drawing primitive
#define RECTS        9  ///< Rectangle drawing primitive

#define DATA(d, i) (d[i])  ///< Macro to access data in byte array

// Commands

const uint8_t CMD_FT812 = 0x09;
const uint8_t CMD_FT812_DATA = 0x0A;

const uint8_t ADDR_EXCLUSIVE = 0x00;

const uint8_t ADDR_CMD_BYTE1 = 0x01;
const uint8_t ADDR_CMD_BYTE2 = 0x02;
const uint8_t ADDR_CMD_BYTE3 = 0x03;

const uint8_t ADDR_TRANSACTION_LEN = 0x04;
const uint8_t ADDR_ADDRESS_BYTE1 = 0x05;
const uint8_t ADDR_ADDRESS_BYTE2 = 0x06;
const uint8_t ADDR_ADDRESS_BYTE3 = 0x07;
const uint8_t ADDR_ADDRESS_BYTE4 = 0x08;

const uint8_t ADDR_ACK_CMD = 0x09;
const uint8_t ADDR_ACK_DATA = 0x0A;

const uint8_t ADDR_DATA_BYTE = 0x00; // ... + ADDR_TRANSACTION_LEN-1 for CMD_FT812_DATA

const ft_mode_t ft_modes[] =
{
  // f_mul, f_div, h_fporch, h_sync, h_bporch, h_visible, v_fporch, v_sync, v_bporch, v_visible
  {6,  2, 16, 96,  48,  640,  11, 2, 31, 480},   //  0: 640x480@57Hz (48MHz)
  {8,  2, 24, 40,  128, 640,  9,  3, 28, 480},   //  1: 640x480@74Hz (64MHz)
  {8,  2, 16, 96,  48,  640,  11, 2, 31, 480},   //  2: 640x480@76Hz (64MHz)
  {5,  1, 40, 128, 88,  800,  1,  4, 23, 600},   //  3: 800x600@60Hz (40MHz)
  {10, 2, 40, 128, 88,  800,  1,  4, 23, 600},   //  4: 800x600@60Hz (80MHz)
  {6,  1, 56, 120, 64,  800,  37, 6, 23, 600},   //  5: 800x600@69Hz (48MHz)
  {7,  1, 32, 64,  152, 800,  1,  3, 27, 600},   //  6: 800x600@85Hz (56MHz)
  {8,  1, 24, 136, 160, 1024, 3,  6, 29, 768},   //  7: 1024x768@59Hz (64MHz)
  {9,  1, 24, 136, 144, 1024, 3,  6, 29, 768},   //  8: 1024x768@67Hz (72MHz)
  {10, 1, 16, 96,  176, 1024, 1,  3, 28, 768},   //  9: 1024x768@76Hz (80MHz)
  {7,  1, 24, 56,  124, 640,  1,  3, 38, 1024},  // 10: 1280/2x1024@60Hz (56MHz)
  {9, 1, 110, 40,  220, 1280, 5,  5, 20, 720},   // 11: 1280x720@58Hz (72MHz)
  {9,  1, 93, 40,  187, 1280, 5,  5, 20, 720},   // 12: 1280x720@60Hz (72MHz)
};

/****************************************************************************/

FT812::FT812(void)
{
}

/****************************************************************************/

void FT812::begin(m_cb act)
{
  action = act;
}

void FT812::spi(bool on)
{
  ctrl_reg = bitWrite(ctrl_reg, 0, on);
  action(CMD_FT812, ADDR_EXCLUSIVE, ctrl_reg);
}

void FT812::vga(bool on)
{
  ctrl_reg = bitWrite(ctrl_reg, 1, on);
  action(CMD_FT812, ADDR_EXCLUSIVE, ctrl_reg);
}

void FT812::reset()
{
  //spi(true);
  commandNoWait(0x68, 0, 0);
  //spi(false);
}

void FT812::command(uint8_t cmd1, uint8_t cmd2, uint8_t cmd3)
{
  //Serial.printf("FT cmd %02x%02x%02x", cmd1, cmd2, cmd3); Serial.println();
  transaction_active = true;
  transaction_start = to_ms_since_boot(get_absolute_time());
  transaction_len = 0;
  data_transaction = false;
  is_write = true;
  action(CMD_FT812, ADDR_CMD_BYTE1, cmd1);
  action(CMD_FT812, ADDR_CMD_BYTE2, cmd2);
  action(CMD_FT812, ADDR_CMD_BYTE3, cmd3);
  wait();
}

void FT812::commandNoWait(uint8_t cmd1, uint8_t cmd2, uint8_t cmd3)
{
  transaction_active = false;
  transaction_len = 0;
  data_transaction = false;
  is_write = true;
  action(CMD_FT812, ADDR_CMD_BYTE1, cmd1);
  action(CMD_FT812, ADDR_CMD_BYTE2, cmd2);
  action(CMD_FT812, ADDR_CMD_BYTE3, cmd3);
  delay(200);
}

void FT812::write(uint32_t addr, uint8_t *data, uint8_t len)
{
  //Serial.printf("FT write %06x len %02x : ", addr, len); 

  transaction_active = true;
  transaction_start = to_ms_since_boot(get_absolute_time());
  data_transaction = true;
  transaction_len = len;
  is_write = true;
  uint8_t addr3 = (uint8_t)((addr & 0x00FF0000) >> 16); 
  addr3 = bitSet(addr3, 7);  // write is "10"
  addr3 = bitClear(addr3, 6);
  uint8_t addr2 = (uint8_t)((addr & 0x0000FF00) >> 8);
  uint8_t addr1 = (uint8_t)((addr & 0x000000FF));  
  action(CMD_FT812, ADDR_TRANSACTION_LEN, len);
  action(CMD_FT812, ADDR_ADDRESS_BYTE1, addr3);
  action(CMD_FT812, ADDR_ADDRESS_BYTE2, addr2);
  action(CMD_FT812, ADDR_ADDRESS_BYTE3, addr1);
  // send data
  for (uint8_t i = 0; i<len; i++) {
    action(CMD_FT812_DATA, i, data[i]);
    //Serial.printf("%02x ", data[i]);
  }
  //Serial.println();
  wait();
}

void FT812::read(uint32_t addr, uint8_t len) 
{
  //Serial.printf("FT read %06x len %02x : ", addr, len); 

  transaction_active = true;
  transaction_start = to_ms_since_boot(get_absolute_time());
  data_transaction = true;
  transaction_len = len;
  is_write = false;
  uint8_t addr3 = (uint8_t)((addr & 0x00FF0000) >> 16); 
  addr3 = bitClear(addr3, 7); // read is "00"
  addr3 = bitClear(addr3, 6);
  uint8_t addr2 = (uint8_t)((addr & 0x0000FF00) >> 8);
  uint8_t addr1 = (uint8_t)((addr & 0x000000FF)); 
  action(CMD_FT812, ADDR_TRANSACTION_LEN, len);
  action(CMD_FT812, ADDR_ADDRESS_BYTE1, addr3);
  action(CMD_FT812, ADDR_ADDRESS_BYTE2, addr2);
  action(CMD_FT812, ADDR_ADDRESS_BYTE3, addr1);
  action(CMD_FT812, ADDR_ADDRESS_BYTE4, 0); // dummy byte
   for (uint8_t i = 0; i<len; i++) {
    action(CMD_FT812_DATA, i, 0); // to read a byte we have to send a dummy byte
  }
  wait();
  //for (uint8_t i = 0; i<len; i++) {
  //  Serial.printf("%02x ", getData(i));
  //}
  //Serial.println();
}

void FT812::setData(uint8_t pos, uint8_t val)
{
  uint8_t count_system = (data_transaction) ? ((is_write) ? 3 : 4) : 3;
  uint8_t count_bytes = (data_transaction) ? count_system + transaction_len : count_system;

  // skip system regs
  if (pos >= count_system) {
    if (!is_write) { // read buffer
      buf[pos-count_system] = val;
    }
  }

  // deactivate transaction progress on last byte received
  if (pos >= count_bytes-1) {
    transaction_active = false;
  }
}

uint8_t FT812::getData(uint8_t pos)
{
  return buf[pos];
}

void FT812::wait()
{
  // todo: add timeout
  while (transaction_active && (to_ms_since_boot(get_absolute_time()) - transaction_start < 100))
  {
    action(0xFF, 0, 0); // NOP
  }
}

/**************************************************************************************************/

void FT812::init(uint8_t m) {

    mode = ft_modes[m];

    // reset
    sendCommand(FT81x_CMD_RST_PULSE);
    delay(300);

    // select clock
    sendCommand(FT81x_CMD_CLKEXT);

    sendCommand(FT81x_CMD_CLKSEL + ((mode.f_mul | 0xC0) << 8)); // f_mul

    // activate
    sendCommand(FT81x_CMD_ACTIVE);

    // wait for boot-up to complete
    delay(100);
    while (read8(FT81x_REG_ID) != 0x7C) {
        __asm__ volatile("nop");
    }
    while (read8(FT81x_REG_CPURESET) != 0x00) {
        __asm__ volatile("nop");
    }

    // configure rgb interface
    write16(FT81x_REG_HSYNC0, mode.h_fporch); // h_fporch
    write16(FT81x_REG_HSYNC1, mode.h_fporch + mode.h_sync); // h_fporch + h_sync
    write16(FT81x_REG_HOFFSET, mode.h_fporch + mode.h_sync + mode.h_bporch); // h_fporch + h_sync + h_bporch
    write16(FT81x_REG_HCYCLE, mode.h_fporch + mode.h_sync + mode.h_bporch + mode.h_visible); // h_visible + h_fporch + h_sync + h_bporch
    write16(FT81x_REG_HSIZE, mode.h_visible); // h_visible

    write16(FT81x_REG_VSYNC0, mode.v_fporch - 1); // v_fporch - 1
    write16(FT81x_REG_VSYNC1, mode.v_fporch + mode.v_sync - 1); // v_fporch + v_sync - 1
    write16(FT81x_REG_VOFFSET, mode.v_fporch + mode.v_sync + mode.v_bporch - 1); // v_fporch + v_syhc + v_bporch - 1
    write16(FT81x_REG_VCYCLE, mode.v_fporch + mode.v_sync + mode.v_bporch + mode.v_visible); // v_visible + v_fporch + v_sync + v_bporch    
    write16(FT81x_REG_VSIZE, mode.v_visible);

    write8(FT81x_REG_PCLK_POL, 0);
    write8(FT81x_REG_CSPREAD, 0);

    // write first display list
    beginDisplayList();
    clear(0);
    swapScreen();

    write8(FT81x_REG_PCLK, mode.f_div); // f_div 1

    // int setup
    write8(FT81x_REG_INT_MASK, 1);
    write8(FT81x_REG_INT_EN, 1);
}

uint16_t FT812::width() {
  return mode.h_visible;
}

uint16_t FT812::height() {
  return mode.v_visible;
}

void FT812::clear(const uint32_t color) {
    startCmd(CLEAR_COLOR(color));
    endCmd(CLEAR(1, 1, 1));
}

void FT812::drawLogo() {
    startCmd(LOGO());
    endCmd(END());
}

void FT812::drawCircle(const int16_t x, const int16_t y, const uint8_t size, const uint32_t color) {
    startCmd(COLOR(color));
    intermediateCmd(POINT_SIZE(size * 16));
    intermediateCmd(BEGIN(POINTS));
    intermediateCmd(VERTEX2F(x * 16, y * 16));
    endCmd(END());
}

void FT812::drawRect(const int16_t x, const int16_t y, const uint16_t width, const uint16_t height, const uint8_t cornerRadius, const uint32_t color) {
    startCmd(COLOR(color));
    intermediateCmd(LINE_WIDTH(cornerRadius * 16));
    intermediateCmd(BEGIN(RECTS));
    intermediateCmd(VERTEX2F(x * 16, y * 16));
    intermediateCmd(VERTEX2F((x + width) * 16, (y + height) * 16));
    endCmd(END());
}

void FT812::drawTri(const int16_t x1, const int16_t y1, const int16_t x2, const int16_t y2, const int16_t x3, const int16_t y3, const uint32_t color, const uint32_t bgcolor) {
    int16_t x_1;
    int16_t y_1;
    int16_t x_2;  // point 2 is highest (smallest y)
    int16_t y_2;  // point 2 is highest (smallest y)
    int16_t x_3;
    int16_t y_3;

    if ((y1 <= y2) && (y1 <= y3)) {  // point 1 is highest on screen
        x_1 = x3;
        y_1 = y3;
        x_2 = x1;
        y_2 = y1;
        x_3 = x2;
        y_3 = y2;
    } else if ((y2 <= y3) && (y2 <= y1)) {  // point 2 is highest
        x_1 = x1;
        y_1 = y1;
        x_2 = x2;
        y_2 = y2;
        x_3 = x3;
        y_3 = y3;
    } else {  // point 3 highest
        x_1 = x2;
        y_1 = y2;
        x_2 = x3;
        y_2 = y3;
        x_3 = x1;
        y_3 = y1;
    }

    if (x_2 <= x_1) {  // one colour wipe (2-3), two bg wipes
        startCmd(COLOR(color));
        intermediateCmd(LINE_WIDTH(1 * 16));
        intermediateCmd(BEGIN(EDGE_STRIP_B));
        intermediateCmd(VERTEX2F(x_2 * 16, y_2 * 16));
        intermediateCmd(VERTEX2F(x_3 * 16, y_3 * 16));
        intermediateCmd(COLOR(bgcolor));
        intermediateCmd(LINE_WIDTH(1 * 16));
        intermediateCmd(BEGIN(EDGE_STRIP_B));
        intermediateCmd(VERTEX2F(x_3 * 16, y_3 * 16));
        intermediateCmd(VERTEX2F(x_1 * 16, y_1 * 16));
        intermediateCmd(VERTEX2F(x_2 * 16, y_2 * 16));
    } else if (x_2 >= x_3) {  // one colour wipe (1-2), two bg wipes
        startCmd(COLOR(color));
        intermediateCmd(LINE_WIDTH(1 * 16));
        intermediateCmd(BEGIN(EDGE_STRIP_B));
        intermediateCmd(VERTEX2F(x_1 * 16, y_1 * 16));
        intermediateCmd(VERTEX2F(x_2 * 16, y_2 * 16));
        intermediateCmd(COLOR(bgcolor));
        intermediateCmd(LINE_WIDTH(1 * 16));
        intermediateCmd(BEGIN(EDGE_STRIP_B));
        intermediateCmd(VERTEX2F(x_2 * 16, y_2 * 16));
        intermediateCmd(VERTEX2F(x_3 * 16, y_3 * 16));
        intermediateCmd(VERTEX2F(x_1 * 16, y_1 * 16));
    } else {  // two colour wipes, one bg wipe
        startCmd(COLOR(color));
        intermediateCmd(LINE_WIDTH(1 * 16));
        intermediateCmd(BEGIN(EDGE_STRIP_B));
        intermediateCmd(VERTEX2F(x_1 * 16, y_1 * 16));
        intermediateCmd(VERTEX2F(x_2 * 16, y_2 * 16));
        intermediateCmd(VERTEX2F(x_3 * 16, y_3 * 16));
        intermediateCmd(COLOR(bgcolor));
        intermediateCmd(LINE_WIDTH(1 * 16));
        intermediateCmd(BEGIN(EDGE_STRIP_B));
        intermediateCmd(VERTEX2F(x_3 * 16, y_3 * 16));
        intermediateCmd(VERTEX2F(x_1 * 16, y_1 * 16));
    }
    endCmd(END());
}

void FT812::drawLine(const int16_t x1, const int16_t y1, const int16_t x2, const int16_t y2, const uint8_t width, const uint32_t color) {
    startCmd(COLOR(color));
    intermediateCmd(LINE_WIDTH(width * 16));
    intermediateCmd(BEGIN(LINES));
    intermediateCmd(VERTEX2F(x1 * 16, y1 * 16));
    intermediateCmd(VERTEX2F(x2 * 16, y2 * 16));
    endCmd(END());
}

void FT812::beginLineStrip(const uint8_t width, const uint32_t color) {
    startCmd(COLOR(color));
    intermediateCmd(LINE_WIDTH(width * 16));
    intermediateCmd(BEGIN(LINE_STRIP));
}

void FT812::addVertex(const int16_t x, const int16_t y) {
    intermediateCmd(VERTEX2F(x * 16, y * 16));
}

void FT812::endLineStrip() {
    endCmd(END());
}

void FT812::drawLetter(const int16_t x, const int16_t y, const uint8_t font, const uint32_t color, const uint8_t letter) {
    uint8_t fontHandle = initBitmapHandleForFont(font);
    startCmd(COLOR(color));
    intermediateCmd(BEGIN(BITMAPS));
    intermediateCmd(VERTEX2II(x, y, fontHandle, letter));
    endCmd(END());
}

void FT812::drawBitmap(const uint32_t offset, const uint16_t x, const uint16_t y, const uint16_t width, const uint16_t height, const uint8_t scale, const uint8_t rot) {
    startCmd(COLOR(FT81x_COLOR_RGB(255, 255, 255)));
    intermediateCmd(BITMAP_SOURCE(FT81x_RAM_G + offset));
    intermediateCmd(BITMAP_LAYOUT(FT81x_BITMAP_LAYOUT_RGB565, (uint32_t)width * 2, (uint32_t)height));  // only supporting one format for now
    intermediateCmd(BITMAP_SIZE(FT81x_BITMAP_SIZE_NEAREST, 0, 0, (uint32_t)width * (uint32_t)scale, (uint32_t)height * (uint32_t)scale));
    intermediateCmd(BEGIN(BITMAPS));
    intermediateCmd(LOADIDENTITY());
    intermediateCmd(SCALE());
    intermediateCmd((uint32_t)scale * 65536);
    intermediateCmd((uint32_t)scale * 65536);

    intermediateCmd(CMD_TRANSLATE());
    intermediateCmd((uint32_t)65536 * (width / 2));
    intermediateCmd((uint32_t)65536 * (height / 2));
    intermediateCmd(CMD_ROTATE());
    intermediateCmd((uint32_t)rot * 65536 / 360);
    intermediateCmd(CMD_TRANSLATE());
    intermediateCmd((uint32_t)65536 * -(width / 2));
    intermediateCmd((uint32_t)65536 * -(height / 2));
    intermediateCmd(SETMATRIX());
    intermediateCmd(BLEND_FUNC(1, 1));
    endCmd(VERTEX2II(x, y, 0, 0));
}

void FT812::overlayBitmap(const uint32_t offset, const uint16_t x, const uint16_t y, const uint16_t width, const uint16_t height, const uint8_t scale, const uint8_t rot) {
    startCmd(COLOR(FT81x_COLOR_RGB(255, 255, 255)));
    intermediateCmd(BITMAP_SOURCE(FT81x_RAM_G + offset));
    intermediateCmd(BITMAP_LAYOUT(FT81x_BITMAP_LAYOUT_RGB565, (uint32_t)width * 2, (uint32_t)height));  // only supporting one format for now
    intermediateCmd(BITMAP_SIZE(FT81x_BITMAP_SIZE_NEAREST, 0, 0, (uint32_t)width * (uint32_t)scale, (uint32_t)height * (uint32_t)scale));
    intermediateCmd(BEGIN(BITMAPS));
    intermediateCmd(LOADIDENTITY());
    intermediateCmd(SCALE());
    intermediateCmd((uint32_t)scale * 65536);
    intermediateCmd((uint32_t)scale * 65536);
    intermediateCmd(CMD_TRANSLATE());
    intermediateCmd((uint32_t)65536 * (width / 2));
    intermediateCmd((uint32_t)65536 * (height / 2));
    intermediateCmd(CMD_ROTATE());
    intermediateCmd((uint32_t)rot * 65536 / 360);
    intermediateCmd(CMD_TRANSLATE());
    intermediateCmd((uint32_t)65536 * -(width / 2));
    intermediateCmd((uint32_t)65536 * -(height / 2));
    intermediateCmd(SETMATRIX());
    intermediateCmd(BLEND_FUNC(1, 1));
    endCmd(VERTEX2II(x, y, 0, 0));
}

void FT812::drawText(const int16_t x, const int16_t y, const uint8_t font, const uint32_t color, const uint16_t options, const char text[]) {
    uint8_t fontHandle = initBitmapHandleForFont(font);
    startCmd(COLOR(color));
    intermediateCmd(LOADIDENTITY());
    intermediateCmd(SCALE());
    intermediateCmd(1 * 65536);
    intermediateCmd(1 * 65536);
    intermediateCmd(SETMATRIX());
    intermediateCmd(TEXT());
    intermediateCmd(x | ((uint32_t)y << 16));
    intermediateCmd(fontHandle | ((uint32_t)options << 16));
    sendText(text);
}

void FT812::drawSpinner(const int16_t x, const int16_t y, const uint16_t style, const uint16_t scale, const uint32_t color) {
    startCmd(COLOR(color));
    intermediateCmd(SPINNER());
    intermediateCmd(x | ((uint32_t)y << 16));
    endCmd(style | ((uint32_t)scale << 16));
}

void FT812::drawButton(const int16_t x, const int16_t y, const int16_t width, const int16_t height, const uint8_t font, const uint32_t textColor, const uint32_t buttonColor, const uint16_t options, const char text[]) {
    uint8_t fontHandle = initBitmapHandleForFont(font);
    startCmd(COLOR(textColor));
    intermediateCmd(FGCOLOR());
    intermediateCmd(buttonColor);
    intermediateCmd(BUTTON());
    intermediateCmd(x | ((uint32_t)y << 16));
    intermediateCmd(width | ((uint32_t)height << 16));
    intermediateCmd(fontHandle | ((uint32_t)options << 16));
    sendText(text);
}

void FT812::drawClock(const int16_t x, const int16_t y, const int16_t radius, const uint32_t handsColor, const uint32_t backgroundColor, const uint16_t options, const uint16_t hours, const uint16_t minutes, const uint16_t seconds) {
    startCmd(COLOR(handsColor));
    intermediateCmd(BGCOLOR());
    intermediateCmd(backgroundColor);
    intermediateCmd(CLOCK());
    intermediateCmd(x | ((uint32_t)y << 16));
    intermediateCmd(radius | ((uint32_t)options << 16));
    intermediateCmd(hours | ((uint32_t)minutes << 16));
    endCmd(seconds);
}

void FT812::drawGauge(const int16_t x, const int16_t y, const int16_t radius, const uint32_t handsColor, const uint32_t backgroundColor, const uint16_t options, const uint8_t major, const uint8_t minor, const uint16_t value, const uint16_t range) {
    startCmd(COLOR(handsColor));
    intermediateCmd(BGCOLOR());
    intermediateCmd(backgroundColor);
    intermediateCmd(GAUGE());
    intermediateCmd(x | ((uint32_t)y << 16));
    intermediateCmd(radius | ((uint32_t)options << 16));
    intermediateCmd(major | ((uint32_t)minor << 16));
    endCmd(value | ((uint32_t)range << 16));
}

void FT812::drawGradient(const int16_t x1, const int16_t y1, const uint32_t color1, const int16_t x2, const int16_t y2, const uint32_t color2) {
    startCmd(GRADIENT());
    intermediateCmd(x1 | ((uint32_t)y1 << 16));
    intermediateCmd(color1);
    intermediateCmd(x2 | ((uint32_t)y2 << 16));
    endCmd(color2);
}

void FT812::drawScrollbar(const int16_t x, const int16_t y, const int16_t width, const int16_t height, const uint32_t foregroundColor, const uint32_t backgroundColor, const uint16_t options, const uint16_t value, const uint16_t size, const uint16_t range) {
    startCmd(COLOR(foregroundColor));
    intermediateCmd(BGCOLOR());
    intermediateCmd(backgroundColor);
    intermediateCmd(SCROLLBAR());
    intermediateCmd(x | ((uint32_t)y << 16));
    intermediateCmd(width | ((uint32_t)height << 16));
    intermediateCmd(options | ((uint32_t)value << 16));
    endCmd(size | ((uint32_t)range << 16));
}

void FT812::drawProgressbar(const int16_t x, const int16_t y, const int16_t width, const int16_t height, const uint32_t foregroundColor, const uint32_t backgroundColor, const uint16_t options, const uint16_t value, const uint16_t range) {
    startCmd(COLOR(foregroundColor));
    intermediateCmd(BGCOLOR());
    intermediateCmd(backgroundColor);
    intermediateCmd(PROGRESSBAR());
    intermediateCmd(x | ((uint32_t)y << 16));
    intermediateCmd(width | ((uint32_t)height << 16));
    intermediateCmd(options | ((uint32_t)value << 16));
    endCmd(range);
}

void FT812::loadImage(const uint32_t offset, const uint32_t size, const uint8_t *data, const bool useProgmem) {
    waitForCommandBuffer();

    startCmd(MEDIAFIFO());
    intermediateCmd(FT81x_RAM_G + 0x100000 - size);
    endCmd(size);

    waitForCommandBuffer();

    writeGRAM(0x100000 - size, size, data, useProgmem);

    write32(FT81x_REG_MEDIAFIFO_WRITE, size - 1);

    startCmd(LOADIMAGE());
    intermediateCmd(FT81x_RAM_G + offset);
    endCmd(16 | 2);  // OPT_MEDIAFIFO | OPT_NODL
}

void FT812::playAudio(const uint32_t offset, const uint32_t size, const uint16_t sampleRate, const uint8_t format, const bool loop) {
    write32(FT81x_REG_PLAYBACK_START, offset);
    write32(FT81x_REG_PLAYBACK_LENGTH, size);
    write16(FT81x_REG_PLAYBACK_FREQ, sampleRate);
    write8(FT81x_REG_PLAYBACK_FORMAT, format);
    write8(FT81x_REG_PLAYBACK_LOOP, loop);
    write8(FT81x_REG_PLAYBACK_PLAY, 1);
}

void FT812::cmd(const uint32_t cmd) {
    uint16_t cmdWrite = FT812::read16(FT81x_REG_CMD_WRITE);
    uint32_t addr = FT81x_RAM_CMD + cmdWrite;
    write32(addr, cmd);
    write16(FT81x_REG_CMD_WRITE, (cmdWrite + 4) % 4096);
}

void FT812::beginDisplayList() {
    waitForCommandBuffer();
    startCmd(DLSTART());
    endCmd(CLEAR(1, 1, 1));
}

void FT812::swapScreen() {
    startCmd(END_DL());
    endCmd(SWAP());
}

void FT812::waitForCommandBuffer() {
    // Wait for circular buffer to catch up
    while (read16(FT81x_REG_CMD_WRITE) != read16(FT81x_REG_CMD_READ)) {
        __asm__ volatile("nop");
    }
}

void FT812::setRotation(const uint8_t rotation) { write8(FT81x_REG_ROTATE, rotation & 0x7); }

bool FT812::isSoundPlaying() {
    return read8(FT81x_REG_PLAY) != 0;
}

void FT812::setAudioVolume(const uint8_t volume) {
    write8(FT81x_REG_VOL_PB, volume);
    write8(FT81x_REG_VOL_SOUND, volume);
}

void FT812::setSound(const uint8_t effect, const uint8_t pitch) {
    write16(FT81x_REG_SOUND, (pitch << 8) | effect);
}

void FT812::playSound() {
    write8(FT81x_REG_PLAY, 1);
}

void FT812::stopSound() {
    setSound(0, 0);
    playSound();
}

inline void FT812::increaseCmdWriteAddress(uint16_t delta) { cmdWriteAddress = (cmdWriteAddress + delta) % 4096; }

inline void FT812::updateCmdWriteAddress() { write16(FT81x_REG_CMD_WRITE, cmdWriteAddress); }

void FT812::sendText(const char text[]) {
    uint32_t data = 0xFFFFFFFF;
    for (uint8_t i = 0; (data >> 24) != 0; i += 4) {
        data = 0;

        if (text[i] != 0) {
            data |= text[i];

            if (text[i + 1] != 0) {
                data |= text[i + 1] << 8;

                if (text[i + 2] != 0) {
                    data |= (uint32_t)text[i + 2] << 16;

                    if (text[i + 3] != 0) {
                        data |= (uint32_t)text[i + 3] << 24;
                    }
                }
            }
        }

        if ((data >> 24) != 0) {
            intermediateCmd(data);
        } else {
            endCmd(data);
        }
    }

    if ((data >> 24) != 0) {
        endCmd(0);
    }
}

uint8_t FT812::initBitmapHandleForFont(uint8_t font) {
    if (font > 31) {
        startCmd(ROMFONT());
        intermediateCmd(14);
        endCmd(font);
        return 14;
    }
    return font;
}

void FT812::writeGRAM(const uint32_t offset, const uint32_t size, const uint8_t *data, const bool useProgmem) {
    uint32_t chunk_size = 128;
    uint32_t chunks = ceil((float)size / chunk_size);
    uint8_t dbuf[128];

    uint32_t cmd = FT81x_RAM_G + offset;

    for (uint32_t j=0; j<chunks; j++) {
      uint32_t tail = size - j*chunk_size;
      uint32_t to = (j == chunks-1) ? tail : chunk_size;
      for (uint32_t i = 0; i < to; i++) {
          dbuf[i] = (useProgmem) ? DATA(data, i + j*chunk_size) : data[i + j*chunk_size];
      }
      write(cmd, dbuf, to);
      cmd += chunk_size; // next cmd + 32
    }
}

void FT812::startCmd(const uint32_t cmd) {
    uint32_t addr = (FT81x_RAM_CMD + cmdWriteAddress);

    transaction_len = 0;
    data_transaction = true;
    // remember cmd_addr
    cmd_addr = addr;
    // add argument to transaction buf
    buf[transaction_len++] = cmd;
    buf[transaction_len++] = cmd >> 8;
    buf[transaction_len++] = cmd >> 16;
    buf[transaction_len++] = cmd >> 24;

    increaseCmdWriteAddress(4);
}

void FT812::intermediateCmd(const uint32_t cmd) {

    buf[transaction_len++] = cmd;
    buf[transaction_len++] = cmd >> 8;
    buf[transaction_len++] = cmd >> 16;
    buf[transaction_len++] = cmd >> 24;

    increaseCmdWriteAddress(4);
}

void FT812::endCmd(const uint32_t cmd) {

    buf[transaction_len++] = cmd;
    buf[transaction_len++] = cmd >> 8;
    buf[transaction_len++] = cmd >> 16;
    buf[transaction_len++] = cmd >> 24;

    write(cmd_addr, buf, transaction_len);

    increaseCmdWriteAddress(4);

    updateCmdWriteAddress();
}

void FT812::sendCommand(const uint32_t cmd) {
  command(cmd >> 16, cmd >> 8, cmd);
}

uint8_t FT812::read8(const uint32_t address) {
    uint32_t cmd = address;
    read(address, 1);
    uint8_t result = getData(0);
    return result;
}

uint16_t FT812::read16(const uint32_t address) {
    read(address, 2);
    uint16_t result = (uint16_t) getData(0);
    result |= ((uint16_t) getData(1) << 8);
    return result;
}

uint32_t FT812::read32(const uint32_t address) {
    read(address, 4);
    uint32_t result = (uint32_t) getData(0);
    result |= ((uint32_t) getData(1) << 8);
    result |= ((uint32_t) getData(2) << 16);
    result |= ((uint32_t) getData(3) << 24);
    return result;
}

void FT812::write8(const uint32_t address, const uint8_t data) {
    uint32_t cmd = address | WRITE;
    uint8_t dbuf[1];
    dbuf[0] = data;
    write(address, dbuf, 1);
}

void FT812::write16(const uint32_t address, const uint16_t data) {
    uint8_t dbuf[2];
    dbuf[0] = (uint8_t) (data & 0x00FF);
    dbuf[1] = (uint8_t) (data >> 8);
    write(address, dbuf, 2);
}

void FT812::write32(const uint32_t address, const uint32_t data) {
    uint8_t dbuf[4];
    dbuf[0] = (uint8_t) (data & 0x000000FF);
    dbuf[1] = (uint8_t) ((data & 0x0000FF00) >> 8);
    dbuf[2] = (uint8_t) ((data & 0x00FF0000) >> 16);
    dbuf[3] = (uint8_t) ((data & 0xFF000000) >> 24);
    write(address, dbuf, 4);
}
