#include "usb_joy.h"
#include "usb_joy_slots.h"
#include "hardware/sync.h"

static usb_joy_slots_t slots;
// irqs off for a few word copies: it never waits through FreeRTOS, unlike pico mutexes, whose
// wakeups from irq context go through the FreeRTOS timer task
static spin_lock_t *joy_lock;

// before main(), as the C++ static initializer it was: the USB host (core1) uses it at once
__attribute__((constructor)) static void joy_lock_init(void) {
  joy_lock = spin_lock_init(spin_lock_claim_unused(true));
}

spin_lock_t *__not_in_flash_func(usb_host_lock)() { // the USB loop runs from RAM
  return joy_lock;
}

bool usb_joy_set(uint8_t address, uint8_t instance, bool xinput, uint16_t buttons) {
  uint32_t save = spin_lock_blocking(joy_lock);
  bool res = usb_joy_slots_set(&slots, address, instance, xinput, buttons);
  spin_unlock(joy_lock, save);
  return res;
}

void usb_joy_remove(uint8_t address, uint8_t instance, bool xinput) {
  uint32_t save = spin_lock_blocking(joy_lock);
  usb_joy_slots_remove(&slots, address, instance, xinput);
  spin_unlock(joy_lock, save);
}

void usb_joy_snapshot(uint16_t buttons[2]) {
  uint32_t save = spin_lock_blocking(joy_lock);
  usb_joy_slots_snapshot(&slots, buttons);
  spin_unlock(joy_lock, save);
}

void usb_joy_ports(usb_joy_port_t ports[2]) {
  uint32_t save = spin_lock_blocking(joy_lock);
  for (uint8_t i = 0; i < 2; i++) {
    if (!usb_joy_slots_get(&slots, i, &ports[i].address, &ports[i].instance, &ports[i].xinput)) ports[i] = (usb_joy_port_t) {0};
  }
  spin_unlock(joy_lock, save);
}
