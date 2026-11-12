#pragma once

// The screens of the bitmap OSD (link v2, 256 x 128, ui/osd2): a header (logo, the name of the
// core or a title, the clock), UI2_ROWS list rows in the 6x12 font, a scrollbar, two hint lines
// in the 5x8 font; boxes and progress bars for the dialogs. Strings are UTF-8 (Cyrillic works).
//
//   y 0..11    header            y 12       line
//   y 13..108  8 rows of 12      y 110      line
//   y 112..127 2 hint lines of 8

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI2_ROWS 8
#define UI2_COLS 41          // characters of a row in the 6x12 font (the scrollbar keeps 4 pixels)

// row flags
#define UI2_SEL 0x01         // the cursor: inverted
#define UI2_DIM 0x02         // disabled: dotted
#define UI2_TITLE 0x04       // a title row of a sub-page (a line under it)
#define UI2_ARROW 0x08       // a page entry: an arrow where the value is

void ui2_screen(const char *title);           // cleared; the header with title (NULL: the core name)
void ui2_clock(void);                          // the clock of the header again (every second)
void ui2_row(uint8_t row, const char *label, const char *value, const char *hotkey, uint8_t flags);
void ui2_text(uint8_t row, const char *text, uint8_t flags); // a row of plain text
void ui2_sep(uint8_t row);                     // a separator line
void ui2_scrollbar(uint16_t top, uint16_t total); // the rows show items top..top+UI2_ROWS-1
void ui2_hint(const char *line1, const char *line2);
// buttons on a row, of equal width from the left; focus = the index of the focused one, -1 none
void ui2_buttons(uint8_t row, const char *const *labels, uint8_t n, int focus);
// a typed field: label, the text with the character at cursor inverted (focused), a note after it
void ui2_field(uint8_t row, const char *label, const char *text, int cursor, bool focused, const char *note);
void ui2_done(void);                           // the screen to the core

// a box in the middle of the screen: two lines; inverse = a title bar
void ui2_box(const char *line1, const char *line2, bool inverse);
// a progress bar in the box: val of max
void ui2_box_progress(const char *title, uint32_t val, uint32_t max);
// the box alone as a popup window (the menu is hidden): call after drawing it
void ui2_box_popup(void);
// a toast of two lines as a popup at the top (zoomed); the bitmap of the menu is overdrawn, the
// menu draws itself again when it opens
void ui2_toast(const char *line1, const char *line2);

#ifdef __cplusplus
}
#endif
