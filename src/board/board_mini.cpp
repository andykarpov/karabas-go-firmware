#include "config.h"

#if HW_ID == HW_ID_MINI || HW_ID == HW_ID_MINIG

#include <Arduino.h>
#include "pio_usb.h"
#include "board.h"
#include "main.h"

#if HW_ID == HW_ID_MINIG
FT812 ft(PIN_MCU_FT_CS, PIN_FT_RESET);
#else
FT812 ft(PIN_MCU_FT_CS);
#endif

void board_init_pins() {
  // unused MCU-FPGA pins (yet)
  pinMode(PIN_MCU_SPI_IO0, INPUT);
  pinMode(PIN_MCU_SPI_IO1, INPUT);
  pinMode(PIN_MCU_SPI_IO4, INPUT);
#if HW_ID == HW_ID_MINIG
  pinMode(PIN_MCU_SPI_IO5, INPUT);
  pinMode(PIN_FT_RESET, OUTPUT);
  pinMode(PIN_ESP_RESET, OUTPUT);
  pinMode(PIN_MIDI_RESET, OUTPUT);
  digitalWrite(PIN_FT_RESET, HIGH);
  digitalWrite(PIN_ESP_RESET, HIGH);
  digitalWrite(PIN_MIDI_RESET, HIGH);
#endif
}

void board_init_io() {
  pinMode(PIN_BTN1, INPUT_PULLUP);
  pinMode(PIN_BTN2, INPUT_PULLUP);
  pinMode(PIN_LED1, OUTPUT); digitalWrite(PIN_LED1, HIGH);
  pinMode(PIN_LED2, OUTPUT); digitalWrite(PIN_LED2, HIGH);
}

uint8_t board_usb_pinout() {
  return PIO_USB_PINOUT_DPDM;
}

void board_joy_read(uint16_t &left, uint16_t &right) {
  left = 0;
  right = 0;
}

void board_reset_peripherals() {
#if HW_ID == HW_ID_MINIG
  digitalWrite(PIN_MIDI_RESET, LOW);
  digitalWrite(PIN_ESP_RESET, LOW);
  delay(50);
  digitalWrite(PIN_MIDI_RESET, HIGH);
  digitalWrite(PIN_ESP_RESET, HIGH);
#endif
}

bool btn_read(uint8_t num) {
  return !digitalRead(num == 0 ? PIN_BTN1 : PIN_BTN2);
}

void led_write(uint8_t num, bool on) {
  digitalWrite(num == 0 ? PIN_LED1 : PIN_LED2, !on);
}

#endif // HW_ID_MINI || HW_ID_MINIG
