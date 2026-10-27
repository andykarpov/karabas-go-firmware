#include "sega.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include "board/pin.h"
#include "ms_time.h"

#define SEGA_CYCLES 8
#define SEGA_READ_MS 5 // >= 3: a 6-button pad needs the time to reset

static unsigned sck_pin, load_pin, data_pin, select_pin;
static uint16_t state_l, state_r;
static bool six_l, six_r;
static uint32_t last_read_ms;

void sega_begin(unsigned sck, unsigned load, unsigned data, unsigned select) {
  sck_pin = sck;
  load_pin = load;
  data_pin = data;
  select_pin = select;
  pin_output(select_pin); gpio_put(select_pin, 1);
  pin_output(sck_pin); gpio_put(sck_pin, 0);
  pin_output(load_pin); gpio_put(load_pin, 0);
  pin_input_pullup(data_pin);
  state_l = state_r = 0;
  six_l = six_r = false;
  last_read_ms = 0;
}

// the 16 bits of the two shift registers
static uint16_t read_pins(void) {
  uint16_t result = 0;
  // latch
  gpio_put(load_pin, 1);
  busy_wait_us_32(2);
  gpio_put(load_pin, 0);
  busy_wait_us_32(2);
  gpio_put(load_pin, 1);
  busy_wait_us_32(2);
  for (uint8_t i = 16; i > 0; i--) {
    if (gpio_get(data_pin)) result |= 1 << (i - 1);
    gpio_put(sck_pin, 1);
    busy_wait_us_32(2);
    gpio_put(sck_pin, 0);
    busy_wait_us_32(2);
  }
  gpio_put(load_pin, 0);
  busy_wait_us_32(2);
  return result;
}

// a line of the pads is active low
static bool low(uint16_t reading, uint8_t bit) {
  return !((reading >> bit) & 1);
}

// bit 0 of the pad: the left pad from bit 0, the right one from bit 8
static void read_cycle(uint8_t cycle) {
  gpio_put(select_pin, cycle % 2);
  busy_wait_us_32(50);
  uint16_t r = read_pins();
  for (uint8_t pad = 0; pad < 2; pad++) {
    uint8_t b = pad * 8;
    uint16_t *st = pad ? &state_r : &state_l;
    bool *six = pad ? &six_r : &six_l;
    switch (cycle) {
      case 2:
        // connected; A and Start only then, against bad reads while the cable goes in / out
        if (low(r, b + 2) && low(r, b + 3)) *st |= SC_CTL_ON;
        if (*st & SC_CTL_ON) {
          if (low(r, b + 4)) *st |= SC_BTN_A;
          if (low(r, b + 5)) *st |= SC_BTN_START;
        }
        break;
      case 3:
        if (low(r, b + 0)) *st |= SC_BTN_UP;
        if (low(r, b + 1)) *st |= SC_BTN_DOWN;
        if (low(r, b + 2)) *st |= SC_BTN_LEFT;
        if (low(r, b + 3)) *st |= SC_BTN_RIGHT;
        if (low(r, b + 4)) *st |= SC_BTN_B;
        if (low(r, b + 5)) *st |= SC_BTN_C;
        break;
      case 4:
        *six = low(r, b + 0) && low(r, b + 1);
        break;
      case 5:
        if (*six) {
          if (low(r, b + 0)) *st |= SC_BTN_Z;
          if (low(r, b + 1)) *st |= SC_BTN_Y;
          if (low(r, b + 2)) *st |= SC_BTN_X;
          if (low(r, b + 3)) *st |= SC_BTN_MODE;
        }
        break;
    }
  }
}

uint16_t sega_state(bool left) {
  if (ms_since(last_read_ms) < SEGA_READ_MS) return left ? state_l : state_r;
  state_l = state_r = 0;
  for (uint8_t cycle = 0; cycle < SEGA_CYCLES; cycle++) read_cycle(cycle);
  // a pad that went away is polled as a 3-button one again
  if (!(state_l & SC_CTL_ON)) six_l = false;
  if (!(state_r & SC_CTL_ON)) six_r = false;
  last_read_ms = ms_now();
  return left ? state_l : state_r;
}
