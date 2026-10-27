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

// karabas: the FT81x driver in C, only what the firmware uses (from the FT812 class).

#include "ft812.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "hardware/gpio.h"
#include "pico/time.h"
#include "spi0_bus.h"
#include "ms_time.h"

// the SPI bus lock of the firmware (src/fpga/link)
void ft812_bus_lock(void);
void ft812_bus_unlock(void);

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
#define MEMCPY()                     0xFFFFFF1D                                                                                                                             ///< Copy memory
#define APPEND()                     0xFFFFFF1E                                                                                                                             ///< Append memory to the display list
#define SLIDER()                     0xFFFFFF10                                                                                                                             ///< Draw slider
#define TOGGLE()                     0xFFFFFF12                                                                                                                             ///< Draw toggle
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
#define VERTEX_FORMAT(frac) ((0x27L << 24) | ((frac) & 7)) ///< Fraction bits of VERTEX2F (4, 1/16 px, by default)
#define BITMAP_SIZE_H(w, h) ((0x29L << 24) | (((uint32_t)(w) & 3) << 2) | ((uint32_t)(h) & 3)) ///< Bits 9-10 of the BITMAP_SIZE width / height
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


// Commands

#define CMD_SPI_CONTROL 0x09
#define ADDR_CONTROL_REGISTER 0x00

// the clock the FT812 gets, f_mul multiplies it (6 -> 48 MHz)
#define FT81x_REF_CLOCK_HZ 8000000UL

// Only the two modes that monitors take as standard, with the FT812 within its 60 MHz:
// at 80 MHz (800x600 at 1:2) it dropped pixels and drew stray rectangles. The 8 MHz input gives
// no 25.175 MHz for 640x480: it runs at 24 MHz with the VESA line (800 clocks) and a shorter
// vertical blanking (500 lines), 60 Hz; a monitor took none of the longer lines or 26.67 MHz.
// 800x600 is VESA exactly, at 1:1: the system clock is the pixel clock, so the frames must stay
// light (the background gradient is a bitmap, CMD_GRADIENT left busy lines unfinished).
static const ft_mode_t ft_modes[] =
{
  // f_mul, f_div, h_fporch, h_sync, h_bporch, h_visible, v_fporch, v_sync, v_bporch, v_visible
  {6,  2, 16, 96,  48,  640, 2,  2, 16, 480},   //  0: 640x480@60Hz (48MHz) pclk 24, 800x500
  {5,  1, 40, 128, 88,  800, 1,  4, 23, 600},   //  1: 800x600@60Hz (40MHz) pclk 40, 1056x628
};

// in the order of ft_modes
static const char *const ft_mode_names[] =
{
  "640x480@60", "800x600@60",
};

_Static_assert(sizeof(ft_mode_names) / sizeof(ft_mode_names[0]) == sizeof(ft_modes) / sizeof(ft_modes[0]),
  "a name for every video mode");


static ft_send_t send_cb;
static uint8_t ctrl_reg;
static uint8_t pin_cs, pin_reset;
static bool has_reset;
static uint32_t init_timer;
static bool slow_spi = false;      // SPI at FT81x_SPI_LOW_CLOCK_SPEED until the system clock runs
static uint32_t clock_hz = 0;      // system clock measured by the last init() (also when it failed)
static uint8_t init_attempts = 0;  // attempts used by the last init()
static char init_failures[96];     // why the failed attempts of the last init() failed
static uint16_t cmd_write_address = 0; // internal pointer to the command buffer of the FT81x chip
static ft_mode_t mode;

static uint32_t spi_hz(void) { return slow_spi ? FT81x_SPI_LOW_CLOCK_SPEED : FT81x_SPI_CLOCK_SPEED; }
uint32_t ft_init_clock_hz(void) { return clock_hz; }
uint8_t ft_init_attempts(void) { return init_attempts; }
const char *ft_init_failures(void) { return init_failures; }

static void ft_power_cycle(void);
static bool ft_power_up(void);
static bool ft_sample_clock(uint32_t *us, uint32_t *cycles);
static void ft_init_failed(const char *fmt, ...);
static void ft_wait_for_command_buffer(void);
static void ft_increase_cmd_write_address(uint16_t delta);
static void ft_update_cmd_write_address(void);
static void ft_send_text(const char text[]);
static uint8_t ft_init_bitmap_handle_for_font(uint8_t font);
static void ft_start_cmd(const uint32_t cmd);
static void ft_intermediate_cmd(const uint32_t cmd);
static void ft_end_cmd(const uint32_t cmd);
static void ft_send_command(const uint32_t cmd);
static uint8_t ft_read8(const uint32_t address);
static uint16_t ft_read16(const uint32_t address);
static uint32_t ft_read32(const uint32_t address);
static void ft_write8(const uint32_t address, const uint8_t data);
static void ft_write16(const uint32_t address, const uint16_t data);
static void ft_write32(const uint32_t address, const uint32_t data);

uint8_t ft_mode_count(void) { return sizeof(ft_modes) / sizeof(ft_modes[0]); }

const char *ft_mode_name(uint8_t m) { return m < ft_mode_count() ? ft_mode_names[m] : "?"; }

void ft_begin(ft_send_t send, uint8_t cs, uint8_t reset) {
    send_cb = send;
    pin_cs = cs;
    pin_reset = reset;
    has_reset = reset != FT_NO_RESET;
    gpio_init(pin_cs); gpio_set_dir(pin_cs, GPIO_OUT); gpio_put(pin_cs, 1);
    if (has_reset) {
        gpio_init(pin_reset); gpio_set_dir(pin_reset, GPIO_OUT); gpio_put(pin_reset, 1);
    }
}

void ft_spi(bool on) {
  ctrl_reg = (ctrl_reg & ~(1u << 0)) | ((on) ? 1u << 0 : 0);
  send_cb(CMD_SPI_CONTROL, ADDR_CONTROL_REGISTER, ctrl_reg);
}

void ft_vga(bool on) {
  ctrl_reg = (ctrl_reg & ~(1u << 1)) | ((on) ? 1u << 1 : 0);
  send_cb(CMD_SPI_CONTROL, ADDR_CONTROL_REGISTER, ctrl_reg);
}

void ft_reset(void) {
    // enable mcu-ft spi
    ft_spi(true);
    if (has_reset) {
        // hard reset by MCU side
        gpio_put(pin_reset, 0); 
        ms_delay(10);
        gpio_put(pin_reset, 1);
    } else {
        // hard reset by FPGA side
        ctrl_reg |= 1u << 4;
        send_cb(CMD_SPI_CONTROL, ADDR_CONTROL_REGISTER, ctrl_reg);
        ms_delay(10);
        ctrl_reg &= ~(1u << 4);
        send_cb(CMD_SPI_CONTROL, ADDR_CONTROL_REGISTER, ctrl_reg);
    }
    // disable mcu-ft spi
    ft_spi(false);
    // disable ft vga switch
    ft_vga(false);
}

// Hardware reset: PD_N low, then high (the same as a power-on reset, the chip comes up in
// SLEEP with the internal oscillator selected). Keeps the spi / vga switches as they are.
static void ft_power_cycle(void) {
    if (has_reset) {
        gpio_put(pin_reset, 0);
        ms_delay(20);
        gpio_put(pin_reset, 1);
    } else {
        // PD_N driven by the FPGA (ctrl register bit 4)
        ctrl_reg |= 1u << 4;
        send_cb(CMD_SPI_CONTROL, ADDR_CONTROL_REGISTER, ctrl_reg);
        ms_delay(20);
        ctrl_reg &= ~(1u << 4);
        send_cb(CMD_SPI_CONTROL, ADDR_CONTROL_REGISTER, ctrl_reg);
    }
    ms_delay(50); // internal regulator and oscillator start-up
}

// Clock setup and wake-up, as in the FT81x programmers guide (host commands at the low SPI clock,
// SPI below 11 MHz until the system clock runs):
//   SLEEP (PLL stopped: CLKSEL only works in this state), CLKEXT, CLKSEL, ACTIVE,
//   then REG_ID = 7Ch and REG_CPURESET = 0.
static bool ft_power_up(void) {
    slow_spi = true;
    ft_send_command(FT81x_CMD_SLEEP);
    ms_delay(20);
    ft_send_command(FT81x_CMD_CLKEXT); // may reset the chip when the clock source changes
    ms_delay(20);
    // Byte2 [7:6] is the PLL range: 0 for 24..36 MHz, 1 for 48..60 MHz (the datasheet gives it by
    // multiplier, for a 12 MHz input; ours is 8 MHz). It was 3, which the datasheet does not define.
    uint32_t pll_hz = (uint32_t) mode.f_mul * FT81x_REF_CLOCK_HZ;
    uint8_t pll_range = pll_hz <= 42000000UL ? 0 : 1;
    ft_send_command(FT81x_CMD_CLKSEL + ((mode.f_mul | (pll_range << 6)) << 8)); // f_mul
    ms_delay(20);
    ft_send_command(FT81x_CMD_ACTIVE);
    ms_delay(50); // PLL lock

    init_timer = ms_now();
    uint8_t id;
    while ((id = ft_read8(FT81x_REG_ID)) != 0x7C) {
        if (ms_since(init_timer) > 1000) { ft_init_failed("REG_ID %02x", id); return false; }
        ms_delay(1);
    }
    uint16_t cpureset;
    while ((cpureset = ft_read16(FT81x_REG_CPURESET)) != 0x00) {
        if (ms_since(init_timer) > 1000) { ft_init_failed("REG_CPURESET %x", cpureset); return false; }
        ms_delay(1);
    }

    // the PLL must run at the clock of the mode: count REG_CLOCK (system clock cycles) over 20 ms.
    // Each read is timed on its own: a task above APP or a wait for the SPI bus between the time
    // and the read made a good clock look 5 % off
    uint32_t t0, c0, t1, c1;
    if (!ft_sample_clock(&t0, &c0)) { ft_init_failed("REG_CLOCK read too slow"); return false; }
    ms_delay(20);
    if (!ft_sample_clock(&t1, &c1)) { ft_init_failed("REG_CLOCK read too slow"); return false; }
    clock_hz = (uint32_t) ((uint64_t) (c1 - c0) * 1000000ULL / (t1 - t0));
    uint32_t want_hz = pll_hz;
    if (clock_hz < want_hz - want_hz / 20 || clock_hz > want_hz + want_hz / 20) { // off by 5 %
        ft_init_failed("clock %lu kHz", (unsigned long) (clock_hz / 1000));
        return false;
    }

    slow_spi = false;
    return true;
}

// one read of REG_CLOCK, at the middle of the time it took; up to 5 tries for one that was not
// held up (a read takes ~0.1 ms at the low SPI clock)
static bool ft_sample_clock(uint32_t *us, uint32_t *cycles) {
    for (int i = 0; i < 5; i++) {
        uint32_t a = time_us_32();
        *cycles = ft_read32(FT81x_REG_CLOCK);
        uint32_t b = time_us_32();
        if (b - a < 300) {
            *us = a + (b - a) / 2;
            return true;
        }
    }
    return false;
}

static void ft_init_failed(const char *fmt, ...) {
    size_t len = strlen(init_failures);
    if (len + 8 >= sizeof(init_failures)) return;
    len += snprintf(init_failures + len, sizeof(init_failures) - len, "%s#%u ", len ? "; " : "", init_attempts);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(init_failures + len, sizeof(init_failures) - len, fmt, ap);
    va_end(ap);
}

uint32_t ft_frames_in(uint32_t ms) {
    uint32_t f0 = ft_read32(FT81x_REG_FRAMES);
    ms_delay(ms);
    return ft_read32(FT81x_REG_FRAMES) - f0;
}

bool ft_init(uint8_t m) {

    mode = ft_modes[m];
    clock_hz = 0;
    init_failures[0] = 0;

    bool up = false;
    for (init_attempts = 1; init_attempts <= 3 && !up; init_attempts++) {
        ft_power_cycle();
        up = ft_power_up();
    }
    init_attempts--;
    slow_spi = false;
    if (!up) return false;

    // after a reset the command fifo starts at 0: follow the chip, not a pointer from before
    cmd_write_address = ft_read16(FT81x_REG_CMD_WRITE);

    // configure vga mode
    ft_write16(FT81x_REG_HCYCLE, mode.h_fporch + mode.h_sync + mode.h_bporch + mode.h_visible); // h_visible + h_fporch + h_sync + h_bporch
    ft_write16(FT81x_REG_HOFFSET, mode.h_fporch + mode.h_sync + mode.h_bporch); // h_fporch + h_sync + h_bporch
    ft_write16(FT81x_REG_HSYNC0, mode.h_fporch); // h_fporch
    ft_write16(FT81x_REG_HSYNC1, mode.h_fporch + mode.h_sync); // h_fporch + h_sync
    ft_write16(FT81x_REG_VCYCLE, mode.v_fporch + mode.v_sync + mode.v_bporch + mode.v_visible); // v_visible + v_fporch + v_sync + v_bporch    
    ft_write16(FT81x_REG_VOFFSET, mode.v_fporch + mode.v_sync + mode.v_bporch - 1); // v_fporch + v_syhc + v_bporch - 1
    ft_write16(FT81x_REG_VSYNC0, mode.v_fporch - 1); // v_fporch - 1
    ft_write16(FT81x_REG_VSYNC1, mode.v_fporch + mode.v_sync - 1); // v_fporch + v_sync - 1
    ft_write8(FT81x_REG_SWIZZLE, 0);
    ft_write8(FT81x_REG_PCLK_POL, 0);
    ft_write8(FT81x_REG_CSPREAD, 0);
    ft_write16(FT81x_REG_HSIZE, mode.h_visible);
    ft_write16(FT81x_REG_VSIZE, mode.v_visible);

    // the system clock of the mode: the chip times the audio samples (REG_PLAYBACK_FREQ) and its
    // timers by this value, not by the real clock, so the sounds played at another speed per mode
    ft_write32(FT81x_REG_FREQUENCY, (uint32_t) mode.f_mul * FT81x_REF_CLOCK_HZ);


    // write first DL
    ft_begin_display_list();
    ft_clear(0);
    ft_swap_screen();    

    // set PCLK
    ft_write8(FT81x_REG_ADAPTIVE_FRAMERATE, 0);
    ft_write32(FT81x_REG_GPIOX_DIR, (uint32_t) 1 << 15); // bit15 - DISP output
    ft_write32(FT81x_REG_GPIOX, (uint32_t)1 << 15 | ((uint32_t)1 << 12) | ((uint32_t)1 << 9)); // bit15 - DISP level, bit12 - PCLK (and others) drive strength, bit9 - INT_N push/pull
    ft_write8(FT81x_REG_PCLK, mode.f_div); // f_div

    // Configure INT
    ft_write8(FT81x_REG_INT_MASK, 1); // FT_INT_SWAP=1
    ft_write8(FT81x_REG_INT_EN, 1);

    return true;
}

uint16_t ft_width(void) {
  return mode.h_visible;
}

uint16_t ft_height(void) {
  return mode.v_visible;
}

void ft_clear(const uint32_t color) {
    ft_start_cmd(CLEAR_COLOR(color));
    ft_end_cmd(CLEAR(1, 1, 1));
}

void ft_draw_circle(const int16_t x, const int16_t y, const uint8_t size, const uint32_t color) {
    ft_start_cmd(COLOR(color));
    ft_intermediate_cmd(POINT_SIZE(size * 16));
    ft_intermediate_cmd(BEGIN(POINTS));
    ft_intermediate_cmd(VERTEX2F(x * 16, y * 16));
    ft_end_cmd(END());
}

void ft_draw_rect(const int16_t x, const int16_t y, const uint16_t width, const uint16_t height, const uint8_t cornerRadius, const uint32_t color) {
    ft_start_cmd(COLOR(color));
    ft_intermediate_cmd(LINE_WIDTH(cornerRadius * 16));
    ft_intermediate_cmd(BEGIN(RECTS));
    ft_intermediate_cmd(VERTEX2F(x * 16, y * 16));
    ft_intermediate_cmd(VERTEX2F((x + width) * 16, (y + height) * 16));
    ft_end_cmd(END());
}

void ft_draw_bitmap(const uint32_t offset, const uint16_t x, const uint16_t y, const uint16_t width, const uint16_t height, const uint8_t scale, const uint8_t rot) {
    ft_start_cmd(COLOR(FT81x_COLOR_RGB(255, 255, 255)));
    ft_intermediate_cmd(BITMAP_SOURCE(FT81x_RAM_G + offset));
    ft_intermediate_cmd(BITMAP_LAYOUT(FT81x_BITMAP_LAYOUT_RGB565, (uint32_t)width * 2, (uint32_t)height));  // only supporting one format for now
    ft_intermediate_cmd(BITMAP_SIZE(FT81x_BITMAP_SIZE_NEAREST, 0, 0, (uint32_t)width * (uint32_t)scale, (uint32_t)height * (uint32_t)scale));
    ft_intermediate_cmd(BEGIN(BITMAPS));
    ft_intermediate_cmd(LOADIDENTITY());
    ft_intermediate_cmd(SCALE());
    ft_intermediate_cmd((uint32_t)scale * 65536);
    ft_intermediate_cmd((uint32_t)scale * 65536);

    ft_intermediate_cmd(CMD_TRANSLATE());
    ft_intermediate_cmd((uint32_t)65536 * (width / 2));
    ft_intermediate_cmd((uint32_t)65536 * (height / 2));
    ft_intermediate_cmd(CMD_ROTATE());
    ft_intermediate_cmd((uint32_t)rot * 65536 / 360);
    ft_intermediate_cmd(CMD_TRANSLATE());
    ft_intermediate_cmd((uint32_t)65536 * -(width / 2));
    ft_intermediate_cmd((uint32_t)65536 * -(height / 2));
    ft_intermediate_cmd(SETMATRIX());
    ft_intermediate_cmd(BLEND_FUNC(1, 1));
    // whole pixels: VERTEX2II only takes 0..511, VERTEX2F at 1/16 px only up to 1023
    ft_intermediate_cmd(VERTEX_FORMAT(0));
    ft_intermediate_cmd(VERTEX2F(x, y));
    ft_intermediate_cmd(VERTEX_FORMAT(4));
    ft_intermediate_cmd(END());
    // back to the defaults, or the text and widgets drawn after it come out scaled and blended
    ft_intermediate_cmd(LOADIDENTITY());
    ft_intermediate_cmd(SETMATRIX());
    ft_end_cmd(BLEND_FUNC(2, 4)); // SRC_ALPHA, ONE_MINUS_SRC_ALPHA
}

void ft_draw_text(const int16_t x, const int16_t y, const uint8_t font, const uint32_t color, const uint16_t options, const char text[]) {
    uint8_t fontHandle = ft_init_bitmap_handle_for_font(font);
    ft_start_cmd(COLOR(color));
    ft_intermediate_cmd(LOADIDENTITY());
    ft_intermediate_cmd(SCALE());
    ft_intermediate_cmd(1 * 65536);
    ft_intermediate_cmd(1 * 65536);
    ft_intermediate_cmd(SETMATRIX());
    ft_intermediate_cmd(TEXT());
    ft_intermediate_cmd(x | ((uint32_t)y << 16));
    ft_intermediate_cmd(fontHandle | ((uint32_t)options << 16));
    ft_send_text(text);
}

void ft_draw_button(const int16_t x, const int16_t y, const int16_t width, const int16_t height, const uint8_t font, const uint32_t textColor, const uint32_t buttonColor, const uint16_t options, const char text[]) {
    uint8_t fontHandle = ft_init_bitmap_handle_for_font(font);
    ft_start_cmd(COLOR(textColor));
    ft_intermediate_cmd(FGCOLOR());
    ft_intermediate_cmd(buttonColor);
    ft_intermediate_cmd(BUTTON());
    ft_intermediate_cmd(x | ((uint32_t)y << 16));
    ft_intermediate_cmd(width | ((uint32_t)height << 16));
    ft_intermediate_cmd(fontHandle | ((uint32_t)options << 16));
    ft_send_text(text);
}

void ft_draw_gauge(const int16_t x, const int16_t y, const int16_t radius, const uint32_t handsColor, const uint32_t backgroundColor, const uint16_t options, const uint8_t major, const uint8_t minor, const uint16_t value, const uint16_t range) {
    ft_start_cmd(COLOR(handsColor));
    ft_intermediate_cmd(BGCOLOR());
    ft_intermediate_cmd(backgroundColor);
    ft_intermediate_cmd(GAUGE());
    ft_intermediate_cmd(x | ((uint32_t)y << 16));
    ft_intermediate_cmd(radius | ((uint32_t)options << 16));
    ft_intermediate_cmd(major | ((uint32_t)minor << 16));
    ft_end_cmd(value | ((uint32_t)range << 16));
}

void ft_draw_bitmap_argb4(const uint32_t offset, const int16_t x, const int16_t y, const uint16_t width, const uint16_t height) {
    ft_start_cmd(COLOR(FT81x_COLOR_RGB(255, 255, 255)));
    ft_intermediate_cmd(BITMAP_SOURCE(FT81x_RAM_G + offset));
    ft_intermediate_cmd(BITMAP_LAYOUT(FT81x_BITMAP_LAYOUT_ARGB4, (uint32_t)width * 2, (uint32_t)height));
    ft_intermediate_cmd(BITMAP_SIZE(FT81x_BITMAP_SIZE_NEAREST, 0, 0, (uint32_t)width, (uint32_t)height));
    ft_intermediate_cmd(BEGIN(BITMAPS));
    ft_intermediate_cmd(VERTEX_FORMAT(0)); // whole pixels: at 1/16 px VERTEX2F only goes to 1023
    ft_intermediate_cmd(VERTEX2F(x, y));
    ft_intermediate_cmd(VERTEX_FORMAT(4));
    ft_end_cmd(END());
}

void ft_draw_bitmap_rgb565_repeat_x(const uint32_t offset, const int16_t x, const int16_t y, const uint16_t bitmapWidth, const uint16_t height, const uint16_t width) {
    ft_start_cmd(COLOR(FT81x_COLOR_RGB(255, 255, 255)));
    ft_intermediate_cmd(BITMAP_SOURCE(FT81x_RAM_G + offset));
    ft_intermediate_cmd(BITMAP_LAYOUT(FT81x_BITMAP_LAYOUT_RGB565, (uint32_t)bitmapWidth * 2, (uint32_t)height));
    ft_intermediate_cmd(BITMAP_SIZE(FT81x_BITMAP_SIZE_NEAREST, 1, 0, (uint32_t)width, (uint32_t)height)); // repeat in x
    ft_intermediate_cmd(BITMAP_SIZE_H((uint32_t)width >> 9, (uint32_t)height >> 9));
    ft_intermediate_cmd(BEGIN(BITMAPS));
    ft_intermediate_cmd(VERTEX_FORMAT(0));
    ft_intermediate_cmd(VERTEX2F(x, y));
    ft_intermediate_cmd(VERTEX_FORMAT(4));
    ft_intermediate_cmd(END());
    ft_end_cmd(BITMAP_SIZE_H(0, 0)); // the other bitmaps of the handle set only the low bits
}

void ft_cmd_memcpy(const uint32_t dest, const uint32_t src, const uint32_t size) {
    ft_start_cmd(MEMCPY());
    ft_intermediate_cmd(dest);
    ft_intermediate_cmd(src);
    ft_end_cmd(size);
}

void ft_cmd_append(const uint32_t offset, const uint32_t size) {
    ft_start_cmd(APPEND());
    ft_intermediate_cmd(FT81x_RAM_G + offset);
    ft_end_cmd(size);
}

uint16_t ft_display_list_size(void) {
    ft_wait_for_command_buffer();
    return ft_read16(FT81x_REG_CMD_DL);
}

void ft_draw_toggle(const int16_t x, const int16_t y, const int16_t width, const uint8_t font, const uint32_t textColor, const uint32_t knobColor, const uint32_t barColor, const uint16_t options, const bool on, const char text[]) {
    uint8_t fontHandle = ft_init_bitmap_handle_for_font(font);
    ft_start_cmd(COLOR(textColor));
    ft_intermediate_cmd(FGCOLOR());
    ft_intermediate_cmd(knobColor);
    ft_intermediate_cmd(BGCOLOR());
    ft_intermediate_cmd(barColor);
    ft_intermediate_cmd(TOGGLE());
    ft_intermediate_cmd(x | ((uint32_t)y << 16));
    ft_intermediate_cmd(width | ((uint32_t)fontHandle << 16));
    ft_intermediate_cmd(options | ((uint32_t)(on ? 65535 : 0) << 16));
    ft_send_text(text);
}

void ft_draw_slider(const int16_t x, const int16_t y, const int16_t width, const int16_t height, const uint32_t fillColor, const uint32_t knobColor, const uint32_t barColor, const uint16_t options, const uint16_t value, const uint16_t range) {
    ft_start_cmd(COLOR(fillColor));
    ft_intermediate_cmd(FGCOLOR());
    ft_intermediate_cmd(knobColor);
    ft_intermediate_cmd(BGCOLOR());
    ft_intermediate_cmd(barColor);
    ft_intermediate_cmd(SLIDER());
    ft_intermediate_cmd(x | ((uint32_t)y << 16));
    ft_intermediate_cmd(width | ((uint32_t)height << 16));
    ft_intermediate_cmd(options | ((uint32_t)value << 16));
    ft_end_cmd(range);
}

void ft_load_image(const uint32_t offset, const uint32_t size, const uint8_t *data) {
    ft_wait_for_command_buffer();

    ft_start_cmd(MEDIAFIFO());
    ft_intermediate_cmd(FT81x_RAM_G + 0x100000 - size);
    ft_end_cmd(size);

    ft_wait_for_command_buffer();

    ft_write_gram(0x100000 - size, size, data);

    ft_write32(FT81x_REG_MEDIAFIFO_WRITE, size - 1);

    ft_start_cmd(LOADIMAGE());
    ft_intermediate_cmd(FT81x_RAM_G + offset);
    ft_end_cmd(16 | 2);  // OPT_MEDIAFIFO | OPT_NODL
}

void ft_play_audio(const uint32_t offset, const uint32_t size, const uint16_t sampleRate, const uint8_t format, const bool loop) {
    ft_write32(FT81x_REG_PLAYBACK_START, offset);
    ft_write32(FT81x_REG_PLAYBACK_LENGTH, size);
    ft_write16(FT81x_REG_PLAYBACK_FREQ, sampleRate);
    ft_write8(FT81x_REG_PLAYBACK_FORMAT, format);
    ft_write8(FT81x_REG_PLAYBACK_LOOP, loop);
    ft_write8(FT81x_REG_PLAYBACK_PLAY, 1);
}

void ft_stop_audio(void) {
    // the programmer guide's way: a zero length, played
    ft_write32(FT81x_REG_PLAYBACK_LENGTH, 0);
    ft_write8(FT81x_REG_PLAYBACK_PLAY, 1);
}

void ft_begin_display_list(void) {
    ft_wait_for_command_buffer();
    ft_start_cmd(DLSTART());
    ft_end_cmd(CLEAR(1, 1, 1));
}

void ft_swap_screen(void) {
    ft_start_cmd(END_DL());
    ft_end_cmd(SWAP());
}

static void ft_wait_for_command_buffer(void) {
    // Wait for circular buffer to catch up
    while (ft_read16(FT81x_REG_CMD_WRITE) != ft_read16(FT81x_REG_CMD_READ)) {
        __asm__ volatile("nop");
    }
}

void ft_set_sound(const uint8_t effect, const uint8_t pitch) {
    ft_write16(FT81x_REG_SOUND, (pitch << 8) | effect);
}

void ft_play_sound(void) {
    ft_write8(FT81x_REG_PLAY, 1);
}

static void ft_increase_cmd_write_address(uint16_t delta) { cmd_write_address = (cmd_write_address + delta) % 4096; }

static void ft_update_cmd_write_address(void) { ft_write16(FT81x_REG_CMD_WRITE, cmd_write_address); }

static void ft_send_text(const char text[]) {
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
            ft_intermediate_cmd(data);
        } else {
            ft_end_cmd(data);
        }
    }

    if ((data >> 24) != 0) {
        ft_end_cmd(0);
    }
}

static uint8_t ft_init_bitmap_handle_for_font(uint8_t font) {
    if (font > 31) {
        ft_start_cmd(ROMFONT());
        ft_intermediate_cmd(14);
        ft_end_cmd(font);
        return 14;
    }
    return font;
}

void ft_write_gram(const uint32_t offset, const uint32_t size, const uint8_t *data) {
    uint32_t cmd = (FT81x_RAM_G + offset) | WRITE;

    ft812_bus_lock();
    spi0_bus_clock(spi_hz());
    gpio_put(pin_cs, 0);

    spi0_bus_byte((cmd >> 16) & 0xFF);
    spi0_bus_byte((cmd >> 8) & 0xFF);
    spi0_bus_byte(cmd & 0xFF);

    for (uint32_t i = 0; i < size; i++) {
        spi0_bus_byte(data[i]);
    }

    gpio_put(pin_cs, 1);
    ft812_bus_unlock();
}

static void ft_start_cmd(const uint32_t cmd) {
    uint32_t addr = (FT81x_RAM_CMD + cmd_write_address) | WRITE;

    ft812_bus_lock();
    spi0_bus_clock(spi_hz());
    gpio_put(pin_cs, 0);

    spi0_bus_byte(addr >> 16);
    spi0_bus_byte(addr >> 8);
    spi0_bus_byte(addr);
    spi0_bus_byte(cmd);
    spi0_bus_byte(cmd >> 8);
    spi0_bus_byte(cmd >> 16);
    spi0_bus_byte(cmd >> 24);

    ft_increase_cmd_write_address(4);
}

static void ft_intermediate_cmd(const uint32_t cmd) {

    spi0_bus_byte(cmd);
    spi0_bus_byte(cmd >> 8);
    spi0_bus_byte(cmd >> 16);
    spi0_bus_byte(cmd >> 24);

    ft_increase_cmd_write_address(4);
}

static void ft_end_cmd(const uint32_t cmd) {

    spi0_bus_byte(cmd);
    spi0_bus_byte(cmd >> 8);
    spi0_bus_byte(cmd >> 16);
    spi0_bus_byte(cmd >> 24);

    gpio_put(pin_cs, 1);
    ft812_bus_unlock();

    ft_increase_cmd_write_address(4);

    ft_update_cmd_write_address();
}

static void ft_send_command(const uint32_t cmd) {

    ft812_bus_lock();
    spi0_bus_clock(FT81x_SPI_LOW_CLOCK_SPEED);
    gpio_put(pin_cs, 0);
    spi0_bus_byte(cmd >> 16);
    spi0_bus_byte(cmd >> 8);
    spi0_bus_byte(cmd);
    gpio_put(pin_cs, 1);
    ft812_bus_unlock();
    //ms_delay(200);
}

static uint8_t ft_read8(const uint32_t address) {
    uint32_t cmd = address | READ;
    ft812_bus_lock();
    spi0_bus_clock(spi_hz());
    gpio_put(pin_cs, 0);
    spi0_bus_byte(cmd >> 16);
    spi0_bus_byte(cmd >> 8);
    spi0_bus_byte(cmd);
    spi0_bus_byte(0x00);  // dummy byte
    uint8_t result = spi0_bus_byte(0x00);
    gpio_put(pin_cs, 1);
    ft812_bus_unlock();
    return result;
}

static uint16_t ft_read16(const uint32_t address) {
    uint32_t cmd = address | READ;
    ft812_bus_lock();
    spi0_bus_clock(spi_hz());
    gpio_put(pin_cs, 0);
    spi0_bus_byte(cmd >> 16);
    spi0_bus_byte(cmd >> 8);
    spi0_bus_byte(cmd);
    spi0_bus_byte(0x00);  // dummy byte
    uint16_t result = spi0_bus_byte(0x00);
    result |= (spi0_bus_byte(0x00) << 8);
    gpio_put(pin_cs, 1);
    ft812_bus_unlock();
    return result;
}

static uint32_t ft_read32(const uint32_t address) {
    uint32_t cmd = address | READ;
    ft812_bus_lock();
    spi0_bus_clock(spi_hz());
    gpio_put(pin_cs, 0);
    spi0_bus_byte(cmd >> 16);
    spi0_bus_byte(cmd >> 8);
    spi0_bus_byte(cmd);
    spi0_bus_byte(0x00);  // dummy byte
    uint32_t result = spi0_bus_byte(0x00);
    result |= (spi0_bus_byte(0x00) << 8);
    result |= ((uint32_t)spi0_bus_byte(0x00) << 16);
    result |= ((uint32_t)spi0_bus_byte(0x00) << 24);
    gpio_put(pin_cs, 1);
    ft812_bus_unlock();
    return result;
}

static void ft_write8(const uint32_t address, const uint8_t data) {
    uint32_t cmd = address | WRITE;
    ft812_bus_lock();
    spi0_bus_clock(spi_hz());
    gpio_put(pin_cs, 0);
    spi0_bus_byte(cmd >> 16);
    spi0_bus_byte(cmd >> 8);
    spi0_bus_byte(cmd);
    spi0_bus_byte(data);
    gpio_put(pin_cs, 1);
    ft812_bus_unlock();
}

static void ft_write16(const uint32_t address, const uint16_t data) {
    uint32_t cmd = address | WRITE;
    ft812_bus_lock();
    spi0_bus_clock(spi_hz());
    gpio_put(pin_cs, 0);
    spi0_bus_byte(cmd >> 16);
    spi0_bus_byte(cmd >> 8);
    spi0_bus_byte(cmd);
    spi0_bus_byte(data);
    spi0_bus_byte(data >> 8);
    gpio_put(pin_cs, 1);
    ft812_bus_unlock();
}

static void ft_write32(const uint32_t address, const uint32_t data) {
    uint32_t cmd = address | WRITE;
    ft812_bus_lock();
    spi0_bus_clock(spi_hz());
    gpio_put(pin_cs, 0);
    spi0_bus_byte(cmd >> 16);
    spi0_bus_byte(cmd >> 8);
    spi0_bus_byte(cmd);
    spi0_bus_byte(data);
    spi0_bus_byte(data >> 8);
    spi0_bus_byte(data >> 16);
    spi0_bus_byte(data >> 24);
    gpio_put(pin_cs, 1);
    ft812_bus_unlock();
}
