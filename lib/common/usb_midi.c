#include "usb_midi.h"

static uint8_t is_dropped(uint8_t status) {
  return status == 0xF0 || status == 0xF7 || status == 0xF8 || status == 0xFE;
}

uint8_t kg_usb_midi_packet(const uint8_t packet[4], uint8_t out[3]) {
  if (packet[0] >> 4) return 0; // another cable
  uint8_t len;
  switch (packet[0] & 0x0F) {
    case 0x2: case 0xC: case 0xD: len = 2; break; // 2-byte system common, program change, channel pressure
    case 0x3: case 0x8: case 0x9: case 0xA: case 0xB: case 0xE: len = 3; break;
    case 0x5: case 0xF: len = 1; break; // 1-byte system common / single byte
    default: return 0; // 0, 1 reserved; 4, 6, 7 SysEx
  }
  if (is_dropped(packet[1])) return 0; // CIN 5 also ends a SysEx with F7 alone
  for (uint8_t i = 0; i < len; i++) out[i] = packet[1 + i];
  return len;
}
