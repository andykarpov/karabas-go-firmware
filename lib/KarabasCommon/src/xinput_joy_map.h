#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// XInput button word / triggers / left stick -> Karabas Sega protocol bits.
// C and Z accept both shoulder and trigger inputs for M30 firmware variants.
static inline uint16_t xinput_joy_map(uint16_t buttons, uint8_t lt, uint8_t rt, int16_t x, int16_t y) {
  uint16_t result = 1; // SC_CTL_ON
  result |= (buttons & 0x001f) << 1; // D-pad and Start
  if (buttons & 0x0020) result |= 4096; // Back -> Mode
  if (buttons & 0x1000) result |= 64;   // A
  if (buttons & 0x2000) result |= 128;  // B
  if (buttons & 0x4000) result |= 512;  // X
  if (buttons & 0x8000) result |= 1024; // Y
  if ((buttons & 0x0200) || rt > 30) result |= 256;  // RB / RT -> C
  if ((buttons & 0x0100) || lt > 30) result |= 2048; // LB / LT -> Z
  if (x < -16384) result |= 8;
  if (x > 16384) result |= 16;
  if (y < -16384) result |= 4;
  if (y > 16384) result |= 2;
  return result;
}

#ifdef __cplusplus
}
#endif
