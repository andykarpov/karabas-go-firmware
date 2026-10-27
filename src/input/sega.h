#pragma once
// Two Sega Mega Drive pads (3 and 6 buttons) on the Karabas Go: their lines come in through two
// shift registers (16 bits: the left pad in bits 0..7, the right in 8..15), select is driven
// directly. Based on SegaController by Jon Thysell (MIT). A read takes 8 select cycles; it is
// done at most every SEGA_READ_MS, else the last state is returned.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  SC_CTL_ON    = 1, // the pad is connected
  SC_BTN_UP    = 2,
  SC_BTN_DOWN  = 4,
  SC_BTN_LEFT  = 8,
  SC_BTN_RIGHT = 16,
  SC_BTN_START = 32,
  SC_BTN_A     = 64,
  SC_BTN_B     = 128,
  SC_BTN_C     = 256,
  SC_BTN_X     = 512,
  SC_BTN_Y     = 1024,
  SC_BTN_Z     = 2048,
  SC_BTN_MODE  = 4096,
  SC_BTN_1     = 128, // Master System compatibility
  SC_BTN_2     = 256  // Master System compatibility
};

void sega_begin(unsigned sck, unsigned load, unsigned data, unsigned select);
// the buttons of the left / right pad (SC_*)
uint16_t sega_state(bool left);

#ifdef __cplusplus
}
#endif
