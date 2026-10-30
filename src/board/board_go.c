#include "config.h"

#if HW_ID == HW_ID_GO

#include "pca9536.h"
#include "input/sega.h"
#include <stdbool.h>
#include <stdint.h>
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
  i2c_bus_lock();
  // a few tries: one failed probe would leave the buttons dead until the next reboot
  for (uint8_t i = 0; i < 5 && !has_extender; i++) {
    if (i) ms_delay(10);
    has_extender = pca9536_present();
  }
  if (!has_extender) {
    d_println("PCA9536 unavailable. Please check soldering.");
  } else {
    d_println("I2C PCA9536 extender detected");
  }

  if (has_extender) {
    // all four pins outputs driven high (LEDs off, active low), as the firmware on Arduino
    // had them: SparkFun's pinMode took INPUT_PULLUP for an output. A pressed button pulls
    // its pin low against the high output, and the input register reads it. As inputs with
    // the chip's 100k pull-ups the buttons were dead on the Go boards.
    pca9536_write(PIN_EXT_LED1, true);
    pca9536_write(PIN_EXT_LED2, true);
    pca9536_write(PIN_EXT_BTN1, true);
    pca9536_write(PIN_EXT_BTN2, true);
    pca9536_outputs(0x0F);
    uint8_t r[4];
    if (pca9536_regs(r)) {
      d_printf("PCA9536 in %02X out %02X pol %02X cfg %02X", r[0], r[1], r[2], r[3]); d_println();
    }
  }
  i2c_bus_unlock();

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
  uint8_t levels;
  i2c_bus_lock();
  bool ok = pca9536_read(&levels);
  i2c_bus_unlock();
  if (!ok) {
    // a failed read is a released button: count them, log at most once a second
    static uint32_t errors = 0, logged = 0;
    errors++;
    if (ms_since(logged) >= 1000) {
      logged = ms_now();
      d_printf("PCA9536 read failed (%lu)", (unsigned long) errors); d_println();
    }
  }
  return ok && !((levels >> (num == 0 ? PIN_EXT_BTN1 : PIN_EXT_BTN2)) & 1);
}

void led_write(uint8_t num, bool on) {
  if (!has_extender) return;
  i2c_bus_lock();
  pca9536_write(num == 0 ? PIN_EXT_LED1 : PIN_EXT_LED2, !on);
  i2c_bus_unlock();
}

#endif // HW_ID_GO
