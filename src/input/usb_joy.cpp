#include "usb_joy.h"
#include "usb_joy_slots.h"
#include "hardware/sync.h"

static UsbJoySlots<2> slots;
// irqs off for a few word copies: it never waits through FreeRTOS, unlike pico mutexes, whose
// wakeups from irq context go through the FreeRTOS timer task
static spin_lock_t *joy_lock = spin_lock_init(spin_lock_claim_unused(true));

spin_lock_t *usb_host_lock() {
  return joy_lock;
}

bool usb_joy_set(uint8_t address, uint8_t instance, bool xinput, uint16_t buttons) {
  uint32_t save = spin_lock_blocking(joy_lock);
  bool res = slots.set(address, instance, xinput, buttons);
  spin_unlock(joy_lock, save);
  return res;
}

void usb_joy_remove(uint8_t address, uint8_t instance, bool xinput) {
  uint32_t save = spin_lock_blocking(joy_lock);
  slots.remove(address, instance, xinput);
  spin_unlock(joy_lock, save);
}

void usb_joy_snapshot(uint16_t buttons[2]) {
  uint32_t save = spin_lock_blocking(joy_lock);
  slots.snapshot(buttons);
  spin_unlock(joy_lock, save);
}
