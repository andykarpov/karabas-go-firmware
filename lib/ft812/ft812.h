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
#pragma once

#include <stdbool.h>
#include <stdint.h>


// Library headers
// Project headers

#define FT81x_COLOR_RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))  ///< Color from RGB values

#define FT81x_ROTATE_LANDSCAPE                   0  ///< Use with setRotation() to rotate screen to landscape
#define FT81x_ROTATE_LANDSCAPE_INVERTED          1  ///< Use with setRotation() to invert and rotate screen to landscape
#define FT81x_ROTATE_PORTRAIT                    2  ///< Use with setRotation() to rotate screen to portrait
#define FT81x_ROTATE_PORTRAIT_INVERTED           3  ///< Use with setRotation() to invert and rotate screen to portrait
#define FT81x_ROTATE_LANDSCAPE_MIRRORED          4  ///< Use with setRotation() to mirror and rotate screen to landscape
#define FT81x_ROTATE_LANDSCAPE_INVERTED_MIRRORED 5  ///< Use with setRotation() to invert, mirror and rotate screen to landscape
#define FT81x_ROTATE_PORTRAIT_MIRRORED           6  ///< Use with setRotation() to mirror and rotate screen to portrait
#define FT81x_ROTATE_PORTRAIT_INVERTED_MIRRORED  7  ///< Use with setRotation() to invert, mirror and rotate screen to portrait

#define FT81x_SPI_CLOCK_SPEED 12000000  ///< FT SPI clock speed
#define FT81x_SPI_LOW_CLOCK_SPEED 1000000  ///< FT SPI low clock speed

#define FT81x_CMD_ACTIVE    0x000000  ///< Switch from Standby/Sleep/PWRDOWN modes to active mode.
#define FT81x_CMD_STANDBY   0x410000  ///< Put FT81x core to standby mode. Clock gate STANDBY off, PLL and Oscillator remain on (default). ACTIVE command to wake up.
#define FT81x_CMD_SLEEP     0x420000  ///< Put FT81x core to sleep mode. Clock gate SLEEP off, PLL and Oscillator off. ACTIVE command to wake up.
#define FT81x_CMD_PWRDOWN   0x430000  ///< Switch off 1.2V core voltage to the digital PWRDOWN core circuits. Clock, PLL and Oscillator off. SPI is alive. ACTIVE command to wake up.
#define FT81x_CMD_CLKEXT    0x440000  ///< Select PLL input from external crystal oscillator or external input clock. No effect if external clock is already selected, otherwise a system reset will be generated.
#define FT81x_CMD_CLKINT    0x480000  ///< Select PLL input from internal relaxation oscillator (default). No effect if internal clock is already selected, otherwise a system reset will be generated.
#define FT81x_CMD_CLKSEL    0x610000  ///< Set clock frequency and PLL range. This command will only be effective when the PLL is stopped (SLEEP mode).
#define FT81x_CMD_RST_PULSE 0x680000  ///< Send reset pulse to FT81x core. The behaviour is the same as POR except that settings done through SPI commands will not be affected.
#define FT81x_CMD_PINDRIVE  0x700000  ///< This will set the drive strength for various pins.

#define FT81x_REG_ID               0x302000  ///< Identification register, always reads as 7Ch
#define FT81x_REG_FRAMES           0x302004  ///< Frame counter, since reset
#define FT81x_REG_CLOCK            0x302008  ///< Clock cycles, since reset
#define FT81x_REG_FREQUENCY        0x30200C  ///< Main clock frequency (Hz)
#define FT81x_REG_RENDERMODE       0x302010  ///< Rendering mode: 0 = normal, 1 = single-line
#define FT81x_REG_SNAPY            0x302014  ///< Scanline select for RENDERMODE 1
#define FT81x_REG_SNAPSHOT         0x302018  ///< Trigger for RENDERMODE 1
#define FT81x_REG_SNAPFORMAT       0x30201C  ///< Pixel format for scanline readout
#define FT81x_REG_CPURESET         0x302020  ///< Graphics, audio and touch engines reset control. Bit2: audio, bit1: touch, bit0: graphics
#define FT81x_REG_TAP_CRC          0x302024  ///< Live video tap crc. Frame CRC is computed every DL SWAP.
#define FT81x_REG_TAP_MASK         0x302028  ///< Live video tap mask
#define FT81x_REG_HCYCLE           0x30202C  ///< Horizontal total cycle count
#define FT81x_REG_HOFFSET          0x302030  ///< Horizontal display start offset
#define FT81x_REG_HSIZE            0x302034  ///< Horizontal display pixel count
#define FT81x_REG_HSYNC0           0x302038  ///< Horizontal sync fall offset
#define FT81x_REG_HSYNC1           0x30203C  ///< Horizontal sync rise offset
#define FT81x_REG_VCYCLE           0x302040  ///< Vertical total cycle count
#define FT81x_REG_VOFFSET          0x302044  ///< Vertical display start offset
#define FT81x_REG_VSIZE            0x302048  ///< Vertical display line count
#define FT81x_REG_VSYNC0           0x30204C  ///< Vertical sync fall offset
#define FT81x_REG_VSYNC1           0x302050  ///< Vertical sync rise offset
#define FT81x_REG_DLSWAP           0x302054  ///< Display list swap control
#define FT81x_REG_ROTATE           0x302058  ///< Screen rotation control. Allow normal/mirrored/inverted for landscape or portrait orientation.
#define FT81x_REG_OUTBITS          0x30205C  ///< Output bit resolution, 3 bits each for R/G/B. Default is 6/6/6 bits for FT810/FT811, and 8/8/8 bits for FT812/FT813 (0b’000 means 8 bits)
#define FT81x_REG_DITHER           0x302060  ///< Output dither enable
#define FT81x_REG_SWIZZLE          0x302064  ///< Output RGB signal swizzle
#define FT81x_REG_CSPREAD          0x302068  ///< Output clock spreading enable
#define FT81x_REG_PCLK_POL         0x30206C  ///< PCLK polarity: 0 = output on PCLK rising edge, 1 = output on PCLK falling edge
#define FT81x_REG_PCLK             0x302070  ///< PCLK frequency divider, 0 = disable
#define FT81x_REG_TAG_X            0x302074  ///< Tag query X coordinate
#define FT81x_REG_TAG_Y            0x302078  ///< Tag query Y coordinate
#define FT81x_REG_TAG              0x30207C  ///< Tag query result
#define FT81x_REG_VOL_PB           0x302080  ///< Volume for playback
#define FT81x_REG_VOL_SOUND        0x302084  ///< Volume for synthesizer sound
#define FT81x_REG_SOUND            0x302088  ///< Sound effect select
#define FT81x_REG_PLAY             0x30208C  ///< Start effect playback
#define FT81x_REG_GPIO_DIR         0x302090  ///< Legacy GPIO pin direction, 0 = input, 1 = output
#define FT81x_REG_GPIO             0x302094  ///< Legacy GPIO read/write
#define FT81x_REG_GPIOX_DIR        0x302098  ///< Extended GPIO pin direction, 0 = input ,1 = output
#define FT81x_REG_GPIOX            0x30209C  ///< Extended GPIO read/write
#define FT81x_REG_INT_FLAGS        0x3020A8  ///< Interrupt flags, clear by read
#define FT81x_REG_INT_EN           0x3020AC  ///< Global interrupt enable, 1=enable
#define FT81x_REG_INT_MASK         0x3020B0  ///< Individual interrupt enable, 1=enable
#define FT81x_REG_PLAYBACK_START   0x3020B4  ///< Audio playback RAM start address
#define FT81x_REG_PLAYBACK_LENGTH  0x3020B8  ///< Audio playback sample length (bytes)
#define FT81x_REG_PLAYBACK_READPTR 0x3020BC  ///< Audio playback current read pointer
#define FT81x_REG_PLAYBACK_FREQ    0x3020C0  ///< Audio playback sampling frequency (Hz)
#define FT81x_REG_PLAYBACK_FORMAT  0x3020C4  ///< Audio playback format
#define FT81x_REG_PLAYBACK_LOOP    0x3020C8  ///< Audio playback loop enable
#define FT81x_REG_PLAYBACK_PLAY    0x3020CC  ///< Start audio playback
#define FT81x_REG_PWM_HZ           0x3020D0  ///< BACKLIGHT PWM output frequency (Hz)
#define FT81x_REG_PWM_DUTY         0x3020D4  ///< BACKLIGHT PWM output duty cycle 0=0%, 128=100%
#define FT81x_REG_MACRO_0          0x3020D8  ///< Display list macro command 0
#define FT81x_REG_MACRO_1          0x3020DC  ///< Display list macro command 1
#define FT81x_REG_CMD_READ         0x3020F8  ///< Command buffer read pointer
#define FT81x_REG_CMD_WRITE        0x3020FC  ///< Command buffer write pointer
#define FT81x_REG_CMD_DL           0x302100  ///< Command display list offset
#define FT81x_REG_BIST_EN          0x302174  ///< BIST memory mapping enable
#define FT81x_REG_TRIM             0x302180  ///< Internal relaxation clock trimming
#define FT81x_REG_ANA_COMP         0x302184  ///< Analogue control register
#define FT81x_REG_SPI_WIDTH        0x302188  ///< QSPI bus width setting, Bit [2]: extra dummy cycle on read, Bit [1:0]: bus width (0=1-bit, 1=2-bit, 2=4-bit)
#define FT81x_REG_DATESTAMP        0x302564  ///< Stamp date code
#define FT81x_REG_CMDB_SPACE       0x302574  ///< Command DL (bulk) space available
#define FT81x_REG_CMDB_WRITE       0x302578  ///< Command DL (bulk) write
#define FT81x_REG_MEDIAFIFO_READ   0x309014  ///< Read pointer for media FIFO in general purpose graphics RAM
#define FT81x_REG_MEDIAFIFO_WRITE  0x309018  ///< Write pointer for media FIFO in general purpose graphics RAM
#define FT81x_REG_ADAPTIVE_FRAMERATE 0x30257C

#define FT81x_RAM_G         0x000000  ///< General purpose graphics RAM
#define FT81x_ROM_FONT      0x1E0000  ///< Font table and bitmap
#define FT81x_ROM_FONT_ADDR 0x2FFFFC  ///< Font table pointer address
#define FT81x_RAM_DL        0x300000  ///< Display List RAM
#define FT81x_RAM_REG       0x302000  ///< Registers
#define FT81x_RAM_CMD       0x308000  ///< Command buffer

#define FT81x_DLSWAP_LINE  0x1  ///< Graphics engine will render the screen immediately after current line is scanned out. It may cause tearing effect.
#define FT81x_DLSWAP_FRAME 0x2  ///< Graphics engine will render the screen immediately after current frame is scanned out. This is recommended in most of cases.

#define FT81x_BITMAP_LAYOUT_ARGB1555 0x1  ///< Bitmap pixel format ARGB1555
#define FT81x_BITMAP_LAYOUT_ARGB4    0x6  ///< Bitmap pixel format ARGB4
#define FT81x_BITMAP_LAYOUT_RGB565   0x7  ///< Bitmap pixel format RGB565

#define FT81x_BITMAP_SIZE_NEAREST  0x0  ///< Bitmap filtering mode: NEAREST
#define FT81x_BITMAP_SIZE_BILINEAR 0x1  ///< Bitmap filtering mode: BILINEAR. For bilinear filtered pixels, the drawing rate is reduced to 1⁄4 pixels per clock.

#define FT81x_OPT_3D        0x0000  ///< 3D effect (CMD_BUTTON,CMD_CLOCK,CMD_KEYS, CMD_GAUGE,CMD_SLIDER, CMD_DIAL, CMD_TOGGLE,CMD_PROGRESS, CMD_SCROLLBAR)
#define FT81x_OPT_FLAT      0x0100  ///< No 3D effect (CMD_BUTTON,CMD_CLOCK,CMD_KEYS, CMD_GAUGE,CMD_SLIDER, CMD_DIAL, CMD_TOGGLE,CMD_PROGRESS, CMD_SCROLLBAR)
#define FT81x_OPT_SIGNED    0x0100  ///< The number is treated as a 32 bit signed integer (CMD_NUMBER)
#define FT81x_OPT_CENTERX   0x0200  ///< Horizontally-centred style (CMD_KEYS,CMD_TEXT , CMD_NUMBER)
#define FT81x_OPT_CENTERY   0x0400  ///< Vertically centred style (CMD_KEYS,CMD_TEXT , CMD_NUMBER)
#define FT81x_OPT_CENTER    0x0600  ///< Horizontally and vertically centred style (CMD_KEYS,CMD_TEXT , CMD_NUMBER)
#define FT81x_OPT_RIGHTX    0x0800  ///< Right justified style (CMD_KEYS,CMD_TEXT , CMD_NUMBER)
#define FT81x_OPT_NOBACK    0x1000  ///< No background drawn (CMD_CLOCK, CMD_GAUGE)
#define FT81x_OPT_NOTICKS   0x2000  ///< No Ticks (CMD_CLOCK, CMD_GAUGE)
#define FT81x_OPT_NOHM      0x4000  ///< No hour and minute hands (CMD_CLOCK)
#define FT81x_OPT_NOPOINTER 0x4000  ///< No pointer (CMD_GAUGE)
#define FT81x_OPT_NOSECS    0x8000  ///< No second hands (CMD_CLOCK)
#define FT81x_OPT_NOHANDS   0xC000  ///< Nohands (CMD_CLOCK)

#define FT81x_AUDIO_FORMAT_LINEAR 0x0  ///< Linear Sample format
#define FT81x_AUDIO_FORMAT_ULAW   0x1  ///< uLaw Sample format
#define FT81x_AUDIO_FORMAT_ADPCM  0x2  ///< 4 bit IMA ADPCM Sample format

#define FT81x_SOUND_SILENCE       0x00  ///< No sound
#define FT81x_SOUND_SQUARE_WAVE   0x01  ///< Square wave
#define FT81x_SOUND_SINE_WAVE     0x02  ///< Sine wave
#define FT81x_SOUND_SAWTOOTH_WAVE 0x03  ///< Sawtooth wave
#define FT81x_SOUND_TRIANGLE_WAVE 0x04  ///< Triangle wave
#define FT81x_SOUND_BEEPING       0x05  ///< Beeping
#define FT81x_SOUND_ALARM         0x06  ///< Alarm
#define FT81x_SOUND_WARBLE        0x07  ///< Warble
#define FT81x_SOUND_CAROUSEL      0x08  ///< Carousel
#define FT81x_SOUND_1_SHORT_PIP   0x10  ///< 1 short pip
#define FT81x_SOUND_2_SHORT_PIPS  0x11  ///< 2 short pip
#define FT81x_SOUND_3_SHORT_PIPS  0x12  ///< 3 short pip
#define FT81x_SOUND_4_SHORT_PIPS  0x13  ///< 4 short pip
#define FT81x_SOUND_5_SHORT_PIPS  0x14  ///< 5 short pip
#define FT81x_SOUND_6_SHORT_PIPS  0x15  ///< 6 short pip
#define FT81x_SOUND_7_SHORT_PIPS  0x16  ///< 7 short pip
#define FT81x_SOUND_8_SHORT_PIPS  0x17  ///< 8 short pip
#define FT81x_SOUND_9_SHORT_PIPS  0x18  ///< 9 short pip
#define FT81x_SOUND_10_SHORT_PIPS 0x19  ///< 10 short pip
#define FT81x_SOUND_11_SHORT_PIPS 0x1A  ///< 11 short pip
#define FT81x_SOUND_12_SHORT_PIPS 0x1B  ///< 12 short pip
#define FT81x_SOUND_13_SHORT_PIPS 0x1C  ///< 13 short pip
#define FT81x_SOUND_14_SHORT_PIPS 0x1D  ///< 14 short pip
#define FT81x_SOUND_15_SHORT_PIPS 0x1E  ///< 15 short pip
#define FT81x_SOUND_16_SHORT_PIPS 0x1F  ///< 16 short pip
#define FT81x_SOUND_DTMF_HASH     0x23  ///< DTMF #
#define FT81x_SOUND_DTMF_STAR     0x2C  ///< DTMF *
#define FT81x_SOUND_DTMF_0        0x30  ///< DTMF 0
#define FT81x_SOUND_DTMF_1        0x31  ///< DTMF 1
#define FT81x_SOUND_DTMF_2        0x32  ///< DTMF 2
#define FT81x_SOUND_DTMF_3        0x33  ///< DTMF 3
#define FT81x_SOUND_DTMF_4        0x34  ///< DTMF 4
#define FT81x_SOUND_DTMF_5        0x35  ///< DTMF 5
#define FT81x_SOUND_DTMF_6        0x36  ///< DTMF 6
#define FT81x_SOUND_DTMF_7        0x37  ///< DTMF 7
#define FT81x_SOUND_DTMF_8        0x38  ///< DTMF 8
#define FT81x_SOUND_DTMF_9        0x39  ///< DTMF 9
#define FT81x_SOUND_HARP          0x40  ///< Harp
#define FT81x_SOUND_XYLOPHONE     0x41  ///< Xylophone
#define FT81x_SOUND_TUBA          0x42  ///< Tuba
#define FT81x_SOUND_GLOCKENSPIEL  0x43  ///< Glockenspiel
#define FT81x_SOUND_ORGAN         0x44  ///< Organ
#define FT81x_SOUND_TRUMPET       0x45  ///< Trumpet
#define FT81x_SOUND_PIANO         0x46  ///< Piano
#define FT81x_SOUND_CHIMES        0x47  ///< Chimes
#define FT81x_SOUND_MUSIC_BOX     0x48  ///< Music box
#define FT81x_SOUND_BELL          0x49  ///< Bell
#define FT81x_SOUND_CLICK         0x50  ///< Click
#define FT81x_SOUND_SWITCH        0x51  ///< Switch
#define FT81x_SOUND_COWBELL       0x52  ///< Cowbell
#define FT81x_SOUND_NOTCH         0x53  ///< Notch
#define FT81x_SOUND_HIHAT         0x54  ///< Hihat
#define FT81x_SOUND_KICKDRUM      0x55  ///< Kickdrum
#define FT81x_SOUND_POP           0x56  ///< Pop
#define FT81x_SOUND_CLACK         0x57  ///< Clack
#define FT81x_SOUND_CHACK         0x58  ///< Chack
#define FT81x_SOUND_MUTE          0x60  ///< Mute
#define FT81x_SOUND_UNMUTE        0x61  ///< Unmute

/****************************************************************************/

// Types
typedef struct
{
  uint8_t f_mul;       // PLL multiplier
  uint8_t f_div;       // Pixel Clock divisor
  uint16_t h_fporch;   // Horizontal front porch size
  uint16_t h_sync;     // Horizontal sync size
  uint16_t h_bporch;   // Horizontal back porch size
  uint16_t h_visible;  // Horizontal visible area size
  uint16_t v_fporch;   // Vertical front porch size
  uint16_t v_sync;     // Vertical sync size
  uint16_t v_bporch;   // Vertical back porch size
  uint16_t v_visible;  // Vertical visible area size
} ft_mode_t;

#ifdef __cplusplus
extern "C" {
#endif

#define FT_NO_RESET 0xFF // ft_begin(): PD_N is driven by the FPGA (ctrl register bit 4)

typedef void (*ft_send_t)(uint8_t cmd, uint8_t addr, uint8_t data);

// the chip select and PD_N pins, the callback that sends the control register to the FPGA
void ft_begin(ft_send_t send, uint8_t cs, uint8_t reset);
void ft_spi(bool on);   // the SPI of the FT812 to the MCU
void ft_vga(bool on);   // the video of the FT812 to the VGA / HDMI output
void ft_reset(void);    // a hardware reset, the SPI and video switches off
// PWRDOWN host command (the running FT812 hums into the audio): video off, then the PLL, the
// oscillator and the core regulator; the SPI and video switches off. ACTIVE or ft_init() wakes it
void ft_power_down(void);
// REG_ID at the low SPI clock (7Ch when the chip runs, not in SLEEP / PWRDOWN); does not wake it
uint8_t ft_read_id(void);
// power cycle, clock and video mode m (0..ft_mode_count()-1); up to 3 attempts
bool ft_init(uint8_t m);
uint32_t ft_init_clock_hz(void);        // the system clock measured by the last ft_init()
uint8_t ft_init_attempts(void);
const char *ft_init_failures(void);     // why the failed attempts failed ("" if none did)
uint32_t ft_frames_in(uint32_t ms);     // frames scanned out in ms (REG_FRAMES), 0: the picture stands
uint8_t ft_mode_count(void);
const char *ft_mode_name(uint8_t m);    // "800x600@60"
uint16_t ft_width(void);
// the running mode draws a pixel every system clock (800x600): no time for heavy lines
bool ft_pclk_is_sysclk(void);
uint16_t ft_height(void);

// a display list: ft_begin_display_list(), the drawing, ft_swap_screen()
void ft_begin_display_list(void);
void ft_swap_screen(void);
uint16_t ft_display_list_size(void);
void ft_clear(uint32_t color);
void ft_draw_circle(int16_t x, int16_t y, uint8_t size, uint32_t color);
// a line through n points (x, y pairs in 1/16 px), radius (half its width) in 1/16 px; the ends
// and the joins are round
void ft_draw_line_strip(const int16_t *xy, uint8_t n, uint16_t radius, uint32_t color);
void ft_draw_rect(int16_t x, int16_t y, uint16_t width, uint16_t height, uint8_t corner_radius, uint32_t color);
void ft_draw_text(int16_t x, int16_t y, uint8_t font, uint32_t color, uint16_t options, const char text[]);
void ft_draw_button(int16_t x, int16_t y, int16_t width, int16_t height, uint8_t font, uint32_t text_color, uint32_t button_color, uint16_t options, const char text[]);
void ft_draw_gauge(int16_t x, int16_t y, int16_t radius, uint32_t hands_color, uint32_t background_color, uint16_t options, uint8_t major, uint8_t minor, uint16_t value, uint16_t range);
void ft_draw_toggle(int16_t x, int16_t y, int16_t width, uint8_t font, uint32_t text_color, uint32_t knob_color, uint32_t bar_color, uint16_t options, bool on, const char text[]);
void ft_draw_slider(int16_t x, int16_t y, int16_t width, int16_t height, uint32_t fill_color, uint32_t knob_color, uint32_t bar_color, uint16_t options, uint16_t value, uint16_t range);
// an RGB565 bitmap in GRAM, scaled and rotated, added on top (additive blending)
void ft_draw_bitmap(uint32_t offset, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t scale, uint8_t rot);
void ft_draw_bitmap_argb4(uint32_t offset, int16_t x, int16_t y, uint16_t width, uint16_t height);
// a vertical gradient across the screen from rgb0 at line y0 to rgb1 at y1, only on these lines
// (CMD_GRADIENT with a scissor)
void ft_draw_gradient_v(int16_t y0, uint32_t rgb0, int16_t y1, uint32_t rgb1);
void ft_cmd_memcpy(uint32_t dest, uint32_t src, uint32_t size);
void ft_cmd_append(uint32_t offset, uint32_t size);   // GRAM at offset into the display list

void ft_write_gram(uint32_t offset, uint32_t size, const uint8_t data[]);
void ft_load_image(uint32_t offset, uint32_t size, const uint8_t data[]); // a JPEG / PNG into GRAM
void ft_play_audio(uint32_t offset, uint32_t size, uint16_t sample_rate, uint8_t format, bool loop);
void ft_stop_audio(void); // stops PCM playback (ft_play_audio)
void ft_set_sound(uint8_t effect, uint8_t pitch);
void ft_play_sound(void);

#ifdef __cplusplus
}
#endif
