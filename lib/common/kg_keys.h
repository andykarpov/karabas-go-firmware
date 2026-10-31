#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A key of AT+KARABAS=key: modifiers and one key joined by '+' ("ctrl+alt+del", "menu+esc",
// "shift+f1", "a", "0x29" a raw HID usage, "alt" a modifier alone: key 0). Names in any case:
// ctrl shift alt menu (= win gui, Left GUI: the Menu key of the firmware), rctrl rshift ralt;
// a-z 0-9 f1-f12 enter esc bksp tab space ins del home end pgup pgdn up down left right
// minus equal lbracket rbracket backslash semicolon quote grave comma dot slash prtsc
// scrlk pause caps num. false: an unknown name or two keys.
bool kg_key_parse(const char *s, uint8_t *mod, uint8_t *key);

// The key of a printable ASCII char (US layout, as kg_hid_ascii): its usage, with Left Shift
// for the shifted ones. false: the char has no key.
bool kg_key_ascii(char c, uint8_t *mod, uint8_t *key);

#ifdef __cplusplus
}
#endif
