#pragma once
// GPIO setup on the pico-sdk, as Arduino's pinMode did it: gpio_init (SIO, input, output
// value 0), then the direction and the pulls. Write with gpio_put, read with gpio_get.

#include <stdbool.h>
#include "hardware/gpio.h"

static inline void pin_output(uint pin) {
  gpio_init(pin);
  gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_4MA);
  gpio_set_dir(pin, GPIO_OUT);
}

static inline void pin_input(uint pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_disable_pulls(pin);
}

static inline void pin_input_pullup(uint pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_pull_up(pin);
}

static inline void pin_input_pulldown(uint pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_pull_down(pin);
}
