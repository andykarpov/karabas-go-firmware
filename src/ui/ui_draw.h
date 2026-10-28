#pragma once

// Drawing helpers of the text OSD screens (32 x 26 chars): the frame of a screen, the hint rows,
// list rows and hotkey labels, so the screens do not place and pad them by hand.

#include <stdbool.h>
#include <stdint.h>
#include "osd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_ROW_TITLE 5      // the title of a screen, a line under it
#define UI_ROW_BODY 7       // the first row under the title line
#define UI_ROW_HINT_LINE 23 // the line over the hints
#define UI_ROW_BODY_LAST (UI_ROW_HINT_LINE - 1)
#define UI_ROW_HINT 24      // two rows of hints
#define UI_COL_LAST (OSD_SIZE_X - 1)
#define UI_COL_CLOCK 24     // the clock of the header, right aligned

// a screen: cleared, the header (logo, builds, the clock), the title with a line under it
void ui_screen(const char *title);
// the line over the hints and the two hint rows, cleared first; NULL leaves a row empty
void ui_hint(const char *line1, const char *line2);
// a row from x to the right edge, the text cut or padded with spaces: black on white when selected
void ui_row(uint8_t x, uint8_t y, const char *text, bool selected);
// rows y1..y2 cleared, white on black
void ui_clear_rows(uint8_t y1, uint8_t y2);
// the body of a screen cleared: the rows between the title line and the hint line
void ui_clear_body(void);
// a framed message in the middle of the body: two lines of up to UI_BOX_TEXT chars in their colors
#define UI_BOX_TEXT 15
void ui_box(const char *line1, uint8_t color1, const char *line2, uint8_t color2);
// the box gone (its rows cleared)
void ui_box_clear(void);
// a hotkey label at the position: key flashing cyan, then rest in white ("S", "etup")
void ui_hotkey(const char *key, const char *rest);

#ifdef __cplusplus
}
#endif
