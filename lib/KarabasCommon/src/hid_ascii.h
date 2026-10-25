#pragma once

#include <stdint.h>

// The printable char of a USB HID keyboard usage (US layout), 0 if the key prints nothing.
// shift: a Shift modifier is down.
char kg_hid_ascii(uint8_t key, bool shift);
