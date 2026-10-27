#pragma once

// Drawing helpers of the text OSD screens (32 x 26 chars): the frame of a screen, the hint rows,
// list rows and hotkey labels, so the screens do not place and pad them by hand.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_ROW_TITLE 5      // the title of a screen, a line under it
#define UI_ROW_BODY 7       // the first row under the title line
#define UI_ROW_HINT_LINE 23 // the line over the hints
#define UI_ROW_HINT 24      // two rows of hints

// a screen: cleared, the header (logo, builds, the clock), the title with a line under it
void ui_screen(const char *title);
// the line over the hints and the two hint rows, cleared first; NULL leaves a row empty
void ui_hint(const char *line1, const char *line2);
// a row from x to the right edge, the text cut or padded with spaces: black on white when selected
void ui_row(uint8_t x, uint8_t y, const char *text, bool selected);
// a hotkey label at the position: key flashing cyan, then rest in white ("S", "etup")
void ui_hotkey(const char *key, const char *rest);

#ifdef __cplusplus
}
#endif
