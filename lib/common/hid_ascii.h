#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The printable char of a USB HID keyboard usage (US layout), 0 if the key prints nothing.
// shift: a Shift modifier is down.
char kg_hid_ascii(uint8_t key, bool shift);

#ifdef __cplusplus
}
#endif
