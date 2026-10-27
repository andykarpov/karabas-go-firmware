#include "config.h"

#if HW_ID == HW_ID_GO

#include <Arduino.h>
#include "pca9536.h"
#include "input/sega.h"
#include "pio_usb.h"
#include "board.h"
#include "main.h"
#include "periph/i2c_bus.h"

uint8_t board_ft_reset_pin() {
  return PIN_FT_RESET;
}

static bool has_extender = false;

void board_init_pins() {
  // unused MCU-FPGA pins (yet)
  pin_input(PIN_MCU_SPI_IO0);
  pin_input_pulldown(PIN_MCU_SPI_IO1); // burst READY from the fpga (undriven reads as not ready)
}

void board_init_io() {
  I2cGuard i2c;
  if (!pca9536_present()) {
    has_extender = false;
    d_println("PCA9536 unavailable. Please check soldering.");
  } else {
    d_println("I2C PCA9536 extender detected");
    has_extender = true;
  }

  if (has_extender) {
    // LEDs off (active low) before they become outputs; the buttons are inputs with the
    // chip's pull-ups (SparkFun's pinMode took INPUT_PULLUP for an output driven high)
    pca9536_write(PIN_EXT_LED1, true);
    pca9536_write(PIN_EXT_LED2, true);
    pca9536_outputs((1 << PIN_EXT_LED1) | (1 << PIN_EXT_LED2));
  }

  sega_begin(PIN_JOY_SCK, PIN_JOY_LOAD, PIN_JOY_DATA, PIN_JOY_P7);
}

uint8_t board_usb_pinout() {
  return PIO_USB_PINOUT_DMDP;
}

void board_joy_read(uint16_t *left, uint16_t *right) {
  (*left) = sega_state(true);
  (*right) = sega_state(false);
}

void board_reset_peripherals() {
}

bool board_has_floppy() {
  return true;
}

bool btn_read(uint8_t num) {
  if (!has_extender) return false;
  I2cGuard i2c;
  uint8_t levels;
  return pca9536_read(&levels) && !((levels >> (num == 0 ? PIN_EXT_BTN1 : PIN_EXT_BTN2)) & 1);
}

void led_write(uint8_t num, bool on) {
  if (!has_extender) return;
  I2cGuard i2c;
  pca9536_write(num == 0 ? PIN_EXT_LED1 : PIN_EXT_LED2, !on);
}

#endif // HW_ID_GO
