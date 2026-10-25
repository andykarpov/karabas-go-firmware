#pragma once
#include <stdint.h>
#include "hardware/sync.h"

// One claimed hardware spinlock for the short critical sections of the USB host data: these
// joystick ports, the keyboard report and the gamepad mapper capture (hid_app.cpp). They are
// never nested. Only 8 spinlocks can be claimed and the SDK takes some; PIO-USB claims one more
// when the USB host starts (alarm_pool_create), and if none is left the USB task panics. So do
// not claim a new spinlock for a new lock, use this one or a FreeRTOS primitive.
spin_lock_t *usb_host_lock();

// The two USB joystick ports, shared by HID (driver ini) and XInput pads.
// A pad takes the first free port when it shows up and keeps it until it is removed.
// Written by the USB task on core1, read by INPUT on core0.

// set the Sega button word of a pad; false if both ports are taken by other pads
bool usb_joy_set(uint8_t address, uint8_t instance, bool xinput, uint16_t buttons);
void usb_joy_remove(uint8_t address, uint8_t instance, bool xinput);
// button words of both ports (0 for a free port)
void usb_joy_snapshot(uint16_t buttons[2]);
