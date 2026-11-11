#pragma once

// The popups and message boxes of the firmware, by kind (docs/link_v2.md 5.3): the callers ask for
// a dialog, the OSD backend draws it. Now the text OSD of link v1 (lib/osd cells, its popup mode);
// the bitmap OSD of link v2 will draw the same kinds.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// toast: the value of a switch changed by a hotkey, over the running core, hidden after 500 ms;
// takes no keys. item: the OSD item (v1 sends it as the popup number, cores may show their own)
void dialog_toast(const char *title, const char *text, uint8_t item);

// progress: title, "Please wait." and a bar val / max; drawn over the menu (the caller opens it)
// or into the popup; takes no keys
void dialog_progress(const char *title, uint32_t val, uint32_t max);
void dialog_loading(uint32_t val, uint32_t max); // "Loading..."
void dialog_hide(void);                          // ends the popup of a progress / the load screen

// the screen of a core load over the running machine (no menu open): a line per ROM
void dialog_load_screen(void);
void dialog_load_rom(uint32_t idx);                 // the next ROM starts
void dialog_load_rom_sent(uint32_t idx, uint32_t sent);
void dialog_load_rom_missing(uint32_t idx);         // its file is not on the card

// a box of two lines in the middle of the screen: an error, a "please wait" (busy), a question
typedef enum {
  DIALOG_ERROR,
  DIALOG_BUSY,
  DIALOG_ASK,   // the caller reads the answer (Enter: yes, Esc: no)
} dialog_kind_t;

void dialog_box(dialog_kind_t kind, const char *title, const char *text);
void dialog_box_clear(void);

#ifdef __cplusplus
}
#endif
