#pragma once
#include <stdint.h>

// Snapshot the two wired XInput ports for the INPUT task.
void xinput_joystick_snapshot(uint16_t buttons[2]);
