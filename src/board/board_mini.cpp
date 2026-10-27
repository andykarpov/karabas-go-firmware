#include "config.h"

#if HW_ID == HW_ID_MINI || HW_ID == HW_ID_MINIG

#include <Arduino.h>
#include "pio_usb.h"
#include "board.h"
#include "main.h"

uint8_t board_ft_reset_pin() {
#if HW_ID == HW_ID_MINIG
  return PIN_FT_RESET;
#else
  return FT_NO_RESET;
#endif
}

void board_init_pins() {
  // unused MCU-FPGA pins (yet)
  pin_input(PIN_MCU_SPI_IO0);
  pin_input_pulldown(PIN_MCU_SPI_IO1); // burst READY from the fpga (undriven reads as not ready)
  pin_input(PIN_MCU_SPI_IO4);
#if HW_ID == HW_ID_MINIG
  pin_input(PIN_MCU_SPI_IO5);
  pin_output(PIN_FT_RESET);
  pin_output(PIN_ESP_RESET);
  pin_output(PIN_MIDI_RESET);
  gpio_put(PIN_FT_RESET, 1);
  gpio_put(PIN_ESP_RESET, 1);
  gpio_put(PIN_MIDI_RESET, 1);
#endif
}

void board_init_io() {
  pin_input_pullup(PIN_BTN1);
  pin_input_pullup(PIN_BTN2);
  pin_output(PIN_LED1); gpio_put(PIN_LED1, 1);
  pin_output(PIN_LED2); gpio_put(PIN_LED2, 1);
}

uint8_t board_usb_pinout() {
  return PIO_USB_PINOUT_DPDM;
}

void board_joy_read(uint16_t *left, uint16_t *right) {
  (*left) = 0;
  (*right) = 0;
}

void board_reset_peripherals() {
#if HW_ID == HW_ID_MINIG
  gpio_put(PIN_MIDI_RESET, 0);
  gpio_put(PIN_ESP_RESET, 0);
  ms_delay(50);
  gpio_put(PIN_MIDI_RESET, 1);
  gpio_put(PIN_ESP_RESET, 1);
#endif
}

bool board_has_floppy() {
  return false;
}

bool btn_read(uint8_t num) {
  return !gpio_get(num == 0 ? PIN_BTN1 : PIN_BTN2);
}

void led_write(uint8_t num, bool on) {
  gpio_put(num == 0 ? PIN_LED1 : PIN_LED2, !on);
}

#endif // HW_ID_MINI || HW_ID_MINIG
