#pragma once
#include <stdint.h>

// The two USB joystick ports, shared by HID (driver ini) and XInput pads.
// A pad takes the first free port when it shows up and keeps it until it is removed.
// Written by the USB task on core1, read by INPUT on core0.

// set the Sega button word of a pad; false if both ports are taken by other pads
bool usb_joy_set(uint8_t address, uint8_t instance, bool xinput, uint16_t buttons);
void usb_joy_remove(uint8_t address, uint8_t instance, bool xinput);
// button words of both ports (0 for a free port)
void usb_joy_snapshot(uint16_t buttons[2]);
