#pragma once

// The screens of the bitmap OSD (link v2, 256 x 128, ui/osd2): a header (the name of the core or
// a title from the left, the clock in the 5x8 font at the right), UI2_ROWS list rows in the 6x12
// font, a scrollbar; boxes and progress bars for the dialogs. Strings are UTF-8 (Cyrillic works).
//
//   y 0..11    header (title, clock), y 12 a line
//   y 14..127  the rows: 9 of 12 pixels; no hint lines

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI2_ROWS ui2_rows() // 9 rows of 12 pixels
uint8_t ui2_rows(void);
#define UI2_COLS 41          // characters of a row in the 6x12 font (the scrollbar keeps 4 pixels)

// row flags
#define UI2_SEL 0x01         // the cursor: inverted
#define UI2_DIM 0x02         // disabled: dotted
#define UI2_TITLE 0x04       // a title row of a sub-page (a line under it)
#define UI2_ARROW 0x08       // a page entry: an arrow where the value is
#define UI2_MARK 0x10        // chosen, the focus elsewhere: a frame
#define UI2_RADIO 0x20       // a radio button before the label (UI2_ON: chosen)
#define UI2_CHECK 0x40       // a check box where the value is (UI2_ON: checked)
#define UI2_ON 0x80

void ui2_screen(const char *title);           // cleared; the header: NULL the core name (the main
                                               // page), else "back" triangle + title (a level under it)
// the header with a note (the extensions of a browser) in place of the clock (NULL: the clock),
// back: the triangle "back"
void ui2_screen_note(const char *title, const char *note, bool back);
// a short screen of n rows, width pixels wide in the middle of the rows area (until ui2_screen);
// 0, 0: from the top left again
void ui2_center(uint8_t n, int width);
void ui2_clock(void);                          // the clock of the header again (every second)
void ui2_row(uint8_t row, const char *label, const char *value, const char *hotkey, uint8_t flags);
void ui2_text(uint8_t row, const char *text, uint8_t flags); // a row of plain text
void ui2_sep(uint8_t row);                     // a separator line
void ui2_scrollbar(uint16_t top, uint16_t total); // the rows show items top..top+UI2_ROWS-1
void ui2_hint(const char *line1, const char *line2); // nothing: no hint lines (kept for the callers)
// buttons on a row, of equal width from the left; focus = the index of the focused one, -1 none
void ui2_buttons(uint8_t row, const char *const *labels, uint8_t n, int focus);
// a typed field: label, the text with the character at cursor inverted (focused), a note after it
void ui2_field(uint8_t row, const char *label, const char *text, int cursor, bool focused, const char *note);
void ui2_done(void);                           // the screen to the core
void ui2_logo(int x, int y);                   // the whole logo, 56 x 16

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
