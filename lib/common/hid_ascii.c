#include "hid_ascii.h"

// usages 0x1e..0x38: 1..0, Enter, Esc, Backspace, Tab, Space, - = [ ] \ (non-US #) ; ' ` , . /
static const char plain[] = "1234567890\0\0\0\0 -=[]\\\\;'`,./";
static const char shifted[] = "!@#$%^&*()\0\0\0\0 _+{}||:\"~<>?";

char kg_hid_ascii(uint8_t key, bool shift) {
  if (key >= 0x04 && key <= 0x1d) return (shift ? 'A' : 'a') + (key - 0x04);
  if (key >= 0x1e && key <= 0x38) return (shift ? shifted : plain)[key - 0x1e];
  if (key >= 0x59 && key <= 0x61) return '1' + (key - 0x59); // keypad 1..9
  switch (key) {
    case 0x54: return '/';  // keypad
    case 0x55: return '*';
    case 0x56: return '-';
    case 0x57: return '+';
    case 0x62: return '0';
    case 0x63: return '.';
    case 0x64: return shift ? '|' : '\\'; // non-US \ |
  }
  return 0;
}
