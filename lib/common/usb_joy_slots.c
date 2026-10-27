#include "usb_joy_slots.h"
#include <string.h>

static bool same(const usb_joy_slot_t *slot, uint8_t address, uint8_t instance, bool xinput) {
  return slot->address == address && slot->instance == instance && slot->xinput == xinput;
}

bool usb_joy_slots_set(usb_joy_slots_t *s, uint8_t address, uint8_t instance, bool xinput, uint16_t buttons) {
  if (!address) return false;
  unsigned free = USB_JOY_SLOTS;
  for (unsigned i = 0; i < USB_JOY_SLOTS; ++i) {
    if (same(&s->slot[i], address, instance, xinput)) {
      s->slot[i].buttons = buttons;
      return true;
    }
    if (!s->slot[i].address && free == USB_JOY_SLOTS) free = i;
  }
  if (free == USB_JOY_SLOTS) return false;
  s->slot[free] = (usb_joy_slot_t) {address, instance, xinput, buttons};
  return true;
}

void usb_joy_slots_remove(usb_joy_slots_t *s, uint8_t address, uint8_t instance, bool xinput) {
  for (unsigned i = 0; i < USB_JOY_SLOTS; ++i)
    if (same(&s->slot[i], address, instance, xinput)) memset(&s->slot[i], 0, sizeof(s->slot[i]));
}

bool usb_joy_slots_get(const usb_joy_slots_t *s, unsigned i, uint8_t *address, uint8_t *instance, bool *xinput) {
  if (i >= USB_JOY_SLOTS || !s->slot[i].address) return false;
  *address = s->slot[i].address;
  *instance = s->slot[i].instance;
  *xinput = s->slot[i].xinput;
  return true;
}

uint8_t usb_joy_slots_snapshot(const usb_joy_slots_t *s, uint16_t buttons[USB_JOY_SLOTS]) {
  uint8_t count = 0;
  for (unsigned i = 0; i < USB_JOY_SLOTS; ++i) {
    buttons[i] = s->slot[i].buttons;
    if (s->slot[i].address) count = i + 1;
  }
  return count;
}
