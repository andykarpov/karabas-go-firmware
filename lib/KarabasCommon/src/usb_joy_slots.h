#pragma once
#include <stdint.h>

// Stable USB joystick ports, keyed by device/interface. Callers provide synchronization.
template <unsigned N> class UsbJoySlots {
  struct Slot { uint8_t address, instance; bool xinput; uint16_t buttons; } slots[N] = {};
public:
  bool set(uint8_t address, uint8_t instance, bool xinput, uint16_t buttons) {
    if (!address) return false;
    unsigned free = N;
    for (unsigned i = 0; i < N; ++i) {
      if (slots[i].address == address && slots[i].instance == instance && slots[i].xinput == xinput) {
        slots[i].buttons = buttons;
        return true;
      }
      if (!slots[i].address && free == N) free = i;
    }
    if (free == N) return false;
    slots[free] = {address, instance, xinput, buttons};
    return true;
  }
  void remove(uint8_t address, uint8_t instance, bool xinput) {
    for (auto &s : slots)
      if (s.address == address && s.instance == instance && s.xinput == xinput) s = {};
  }
  // the pad on port i; false if the port is free
  bool get(unsigned i, uint8_t &address, uint8_t &instance, bool &xinput) const {
    if (i >= N || !slots[i].address) return false;
    address = slots[i].address;
    instance = slots[i].instance;
    xinput = slots[i].xinput;
    return true;
  }
  uint8_t snapshot(uint16_t *buttons) const {
    uint8_t count = 0;
    for (unsigned i = 0; i < N; ++i) {
      buttons[i] = slots[i].buttons;
      if (slots[i].address) count = i + 1;
    }
    return count;
  }
};
