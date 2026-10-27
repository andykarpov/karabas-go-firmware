#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Stable USB joystick ports, keyed by device/interface. Callers provide synchronization.
#define USB_JOY_SLOTS 2

typedef struct {
  uint8_t address, instance;
  bool xinput;
  uint16_t buttons;
} usb_joy_slot_t;

typedef struct {
  usb_joy_slot_t slot[USB_JOY_SLOTS];
} usb_joy_slots_t;

// the buttons of a pad; a new pad takes the first free port. false if address is 0 or all are taken
bool usb_joy_slots_set(usb_joy_slots_t *s, uint8_t address, uint8_t instance, bool xinput, uint16_t buttons);
void usb_joy_slots_remove(usb_joy_slots_t *s, uint8_t address, uint8_t instance, bool xinput);
// the pad on port i; false if the port is free
bool usb_joy_slots_get(const usb_joy_slots_t *s, unsigned i, uint8_t *address, uint8_t *instance, bool *xinput);
// the buttons of all ports; returns the number of ports up to the last one in use
uint8_t usb_joy_slots_snapshot(const usb_joy_slots_t *s, uint16_t buttons[USB_JOY_SLOTS]);

#ifdef __cplusplus
}
#endif
