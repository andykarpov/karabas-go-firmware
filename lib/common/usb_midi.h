#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// USB-MIDI 1.0 event packets (4 bytes: cable number << 4 | CIN, then 3 MIDI bytes) into
// a plain MIDI byte stream, without running status. Only cable 0 is taken. SysEx,
// Timing Clock (F8) and Active Sensing (FE) are dropped: each byte costs a link frame.
// Returns the number of bytes written to out (0..3).
uint8_t kg_usb_midi_packet(const uint8_t packet[4], uint8_t out[3]);

#ifdef __cplusplus
}
#endif
