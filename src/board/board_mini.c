#include "config.h"

#if HW_ID == HW_ID_MINI || HW_ID == HW_ID_MINIG

#include <stdbool.h>
#include <stdint.h>
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
  pin_input_pulldown(PIN_MCU_SPI_IO1); // v1 burst READY / v2 INT from the fpga (undriven reads as not ready / no interrupt)
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

int board_fdd_chng() {
  return -1;
}

bool btn_read(uint8_t num) {
  return !gpio_get(num == 0 ? PIN_BTN1 : PIN_BTN2);
}

static uint8_t led_ovr_mask = 0, led_ovr_on = 0;
static uint8_t led_fw_on = 0; // the last state the firmware wrote, put back when a core lets an LED go

void led_write(uint8_t num, bool on) {
  if (on) led_fw_on |= 1 << num; else led_fw_on &= ~(1 << num);
  if (led_ovr_mask & (1 << num)) on = (led_ovr_on >> num) & 1;
  gpio_put(num == 0 ? PIN_LED1 : PIN_LED2, !on);
}

void led_override(uint8_t mask, uint8_t on) {
  uint8_t changed = led_ovr_mask | (mask & 3);
  led_ovr_mask = mask & 3;
  led_ovr_on = on & 3;
  // the held LEDs at their state, the released ones back at the state of the firmware
  uint8_t fw = led_fw_on;
  for (uint8_t i = 0; i < 2; i++) if (changed & (1 << i)) led_write(i, (fw >> i) & 1);
}

#endif // HW_ID_MINI || HW_ID_MINIG
