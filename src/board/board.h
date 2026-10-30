#pragma once

// Board abstraction: everything that differs between Karabas Go / Mini / Mini G
// lives in src/board/board_*.cpp. The rest of the firmware must not use #if HW_ID.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"
#include "ft812.h"

#ifdef __cplusplus
extern "C" {
#endif

// the PD_N pin of the FT812 driven by the MCU, or FT_NO_RESET (driven by the FPGA)
uint8_t board_ft_reset_pin();

// the I2C bus clock: 100 kHz on the Karabas Go (its add-ons on a cable, 4k7 pull-ups: the
// rise time is past the 300 ns of fast mode), 400 kHz on the Mini boards
uint32_t board_i2c_hz();

// early gpio setup: unused mcu-fpga lines, peripheral reset lines (before I2C)
void board_init_pins();

// buttons, leds, joysticks, I2C io extender (after I2C is up)
void board_init_io();

// PIO-USB host D+/D- pin order (PIO_USB_PINOUT_*)
uint8_t board_usb_pinout();

// read on-board joystick ports (0 when the board has none)
void board_joy_read(uint16_t *left, uint16_t *right);

// pulse reset of the on-board peripherals that follow the core reset (ESP, MIDI)
void board_reset_peripherals();

// the board has a floppy connector (Karabas Go): F slots can be a physical drive
bool board_has_floppy();

bool btn_read(uint8_t num);
void led_write(uint8_t num, bool on);

#ifdef __cplusplus
}
#endif
