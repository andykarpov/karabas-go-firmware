#include "config.h"

#if HW_ID == HW_ID_GO

#include <Arduino.h>
#include <SparkFun_PCA9536_Arduino_Library.h>
#include <SegaController.h>
#include "pio_usb.h"
#include "board.h"
#include "main.h"
#include "periph/i2c_bus.h"

FT812 ft(PIN_MCU_FT_CS, PIN_FT_RESET);

static PCA9536 extender;
static SegaController sega;
static bool has_extender = false;

void board_init_pins() {
  // unused MCU-FPGA pins (yet)
  pin_input(PIN_MCU_SPI_IO0);
  pin_input_pulldown(PIN_MCU_SPI_IO1); // burst READY from the fpga (undriven reads as not ready)
}

void board_init_io() {
  I2cGuard i2c;
  if (extender.begin() == false) {
    has_extender = false;
    d_println("PCA9536 unavailable. Please check soldering.");
  } else {
    d_println("I2C PCA9536 extender detected");
    has_extender = true;
  }

  if (has_extender) {
    extender.pinMode(PIN_EXT_BTN1, INPUT_PULLUP);
    extender.pinMode(PIN_EXT_BTN2, INPUT_PULLUP);
    extender.pinMode(PIN_EXT_LED1, OUTPUT);
    extender.pinMode(PIN_EXT_LED2, OUTPUT);
    extender.digitalWrite(PIN_EXT_LED1, HIGH);
    extender.digitalWrite(PIN_EXT_LED2, HIGH);
  }

  sega.begin(PIN_JOY_SCK, PIN_JOY_LOAD, PIN_JOY_DATA, PIN_JOY_P7);
}

uint8_t board_usb_pinout() {
  return PIO_USB_PINOUT_DMDP;
}

void board_joy_read(uint16_t &left, uint16_t &right) {
  left = sega.getState(true);
  right = sega.getState(false);
}

void board_reset_peripherals() {
}

bool board_has_floppy() {
  return true;
}

bool btn_read(uint8_t num) {
  if (!has_extender) return false;
  I2cGuard i2c;
  return !extender.digitalRead(num == 0 ? PIN_EXT_BTN1 : PIN_EXT_BTN2);
}

void led_write(uint8_t num, bool on) {
  if (!has_extender) return;
  I2cGuard i2c;
  extender.digitalWrite(num == 0 ? PIN_EXT_LED1 : PIN_EXT_LED2, !on);
}

#endif // HW_ID_GO
