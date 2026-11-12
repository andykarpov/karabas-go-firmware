#include "main.h"
#include "ui/ui2.h"
#include "ui/osd2.h"

// the logo of About: the OLED logo of bitmaps.h at its own size
static const uint8_t logo_full[112] = { // 56x16, rows MSB first
  0x8e, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x80, 0x00,
  0x00, 0x00, 0x80, 0x00, 0x00, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0x82, 0x02, 0x80, 0x02,
  0x82, 0x02, 0x80, 0x82, 0xfe, 0x80, 0xfe, 0x82, 0xfe, 0xfe, 0x82, 0x82, 0x80, 0x82, 0x82, 0x82,
  0x02, 0x82, 0xfe, 0x80, 0xfe, 0xfe, 0xfe, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0xee, 0xe0,
  0x00, 0x82, 0xfe, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x82, 0x00, 0x00, 0x00, 0x3c, 0xb2,
  0xbe, 0x82, 0x00, 0x00, 0x00, 0x2a, 0xaa, 0x82, 0x82, 0x00, 0x00, 0x00, 0x2a, 0xaa, 0xfe, 0xfe,
};


#define F_SMALL u8g2_font_5x8_t_cyrillic

#define F_MAIN u8g2_font_6x12_t_cyrillic
#define ROW_H 12

#define Y_HEAD_LINE 12
#define Y_ROWS 14
#define X_SCROLL 252
#define Y_END OSD2_H // no hint lines (the user: they only take room)

uint8_t ui2_rows(void) { return (Y_END - Y_ROWS + 1) / ROW_H; }
static int x_value(void) { return 2 + 14 * 6; }

static void clock_text(char *out, size_t size) {
  snprintf(out, size, "%02u:%02u:%02u", rtc_get_hour(), rtc_get_minute(), rtc_get_second());
}

// a string drawn from x, cut at x_end (whole UTF-8 characters)
static void text_to(int x, int y, int x_end, const char *s) {
  char t[64];
  snprintf(t, sizeof(t), "%s", s ? s : "");
  while (t[0] && x + u8g2_GetUTF8Width(&osd2, t) > x_end) {
    size_t n = strlen(t);
    do { n--; } while (n > 0 && (t[n] & 0xC0) == 0x80);
    t[n] = '\0';
  }
  u8g2_DrawUTF8(&osd2, x, y, t);
}

static void begin_draw(void) {
  osd2_begin();
  u8g2_SetFontPosTop(&osd2);
  u8g2_SetFontMode(&osd2, 1);
  u8g2_SetDrawColor(&osd2, 1);
  u8g2_SetBitmapMode(&osd2, 1);
}

// the header: the name from the left edge (a title: after a triangle "back"), the clock in the
// small font at the right
void ui2_screen(const char *title) {
  begin_draw();
  u8g2_ClearBuffer(&osd2);
  char c[12];
  clock_text(c, sizeof(c));
  u8g2_SetFont(&osd2, F_SMALL);
  int cw = u8g2_GetStrWidth(&osd2, c);
  text_to(OSD2_W - cw, 2, OSD2_W, c);
  u8g2_SetFont(&osd2, F_MAIN);
  int x = 1;
  if (title) { // a level under the main page: a triangle "back" before the title
    for (int i = 0; i < 6; i++) u8g2_DrawVLine(&osd2, 1 + i, 5 - i, 2 * i + 1); // 6 x 11, tip left
    x = 10;
  }
  text_to(x, 0, OSD2_W - 6 - cw, title ? title : core.name);
  u8g2_DrawHLine(&osd2, 0, Y_HEAD_LINE, OSD2_W);
}

void ui2_clock(void) {
  if (!osd2_active()) return;
  begin_draw();
  char c[12];
  clock_text(c, sizeof(c));
  u8g2_SetFont(&osd2, F_SMALL);
  int w = u8g2_GetStrWidth(&osd2, c);
  u8g2_SetDrawColor(&osd2, 0);
  u8g2_DrawBox(&osd2, OSD2_W - w, 0, w, Y_HEAD_LINE);
  u8g2_SetDrawColor(&osd2, 1);
  text_to(OSD2_W - w, 2, OSD2_W, c);
  osd2_flush();
}

static int row_y(uint8_t row) { return Y_ROWS + row * ROW_H; }

static void row_bar(uint8_t row, uint8_t flags) {
  u8g2_SetDrawColor(&osd2, 1);
  if (flags & UI2_SEL) {
    u8g2_DrawBox(&osd2, 0, row_y(row) - 1, X_SCROLL - 1, ROW_H);
    u8g2_SetDrawColor(&osd2, 0);
  }
}

void ui2_row(uint8_t row, const char *label, const char *value, const char *hotkey, uint8_t flags) {
  if (row >= ui2_rows()) return;
  int y = row_y(row);
  row_bar(row, flags);
  int hk_x = X_SCROLL - 2;
  if (hotkey && hotkey[0]) {
    u8g2_SetFont(&osd2, F_SMALL);
    int w = u8g2_GetUTF8Width(&osd2, hotkey);
    hk_x = X_SCROLL - 3 - w;
    u8g2_DrawUTF8(&osd2, hk_x, y + (ROW_H - 8) / 2 - 1, hotkey);
  }
  // a disabled row in the small font: readable, but plainly not the others
  bool dimmed = flags & UI2_DIM;
  u8g2_SetFont(&osd2, dimmed ? F_SMALL : F_MAIN);
  int ty = dimmed ? y + (ROW_H - 8) / 2 - 1 : y;
  // without a value the label goes up to the hotkey; a page entry has its arrow at the right
  int arrow_x = hk_x - 10;
  text_to(2, ty, value ? x_value() - 4 : ((flags & UI2_ARROW) ? arrow_x - 4 : hk_x - 4), label);
  if (flags & UI2_ARROW) {
    text_to(arrow_x, ty, hk_x, ">");
  } else if (value) {
    text_to(x_value(), ty, hk_x - 4, value);
  }
  u8g2_SetDrawColor(&osd2, 1);
}

void ui2_text(uint8_t row, const char *text, uint8_t flags) {
  if (row >= ui2_rows()) return;
  int y = row_y(row);
  row_bar(row, flags);
  bool dimmed = flags & UI2_DIM;
  u8g2_SetFont(&osd2, dimmed ? F_SMALL : F_MAIN);
  text_to(2, dimmed ? y + (ROW_H - 8) / 2 - 1 : y, X_SCROLL - 2, text);
  u8g2_SetDrawColor(&osd2, 1);
  if (flags & UI2_TITLE) u8g2_DrawHLine(&osd2, 0, y + ROW_H - 2, X_SCROLL - 1);
}

void ui2_sep(uint8_t row) {
  if (row >= ui2_rows()) return;
  int y = row_y(row) + ROW_H / 2 - 1;
  for (int x = 2; x < X_SCROLL - 2; x += 2) u8g2_DrawPixel(&osd2, x, y);
}

void ui2_scrollbar(uint16_t top, uint16_t total) {
  uint8_t rows = ui2_rows();
  if (total <= rows) return;
  int y0 = Y_ROWS - 1, h = rows * ROW_H;
  for (int y = y0; y < y0 + h; y += 2) u8g2_DrawPixel(&osd2, X_SCROLL + 1, y); // the track, dotted
  int th = h * rows / total;
  if (th < 4) th = 4;
  int ty = y0 + (h - th) * top / (total - rows);
  u8g2_DrawBox(&osd2, X_SCROLL, ty, 3, th);
}

void ui2_hint(const char *line1, const char *line2) {
  (void) line1; (void) line2; // the v2 screens have no hint lines
}

#define BTN_W 60

void ui2_buttons(uint8_t row, const char *const *labels, uint8_t n, int focus) {
  if (row >= ui2_rows()) return;
  int y = row_y(row) - 1;
  u8g2_SetFont(&osd2, F_MAIN);
  for (uint8_t i = 0; i < n; i++) {
    int x = 2 + i * (BTN_W + 4);
    int w = u8g2_GetUTF8Width(&osd2, labels[i]);
    if ((int) i == focus) {
      u8g2_DrawBox(&osd2, x, y, BTN_W, ROW_H);
      u8g2_SetDrawColor(&osd2, 0);
    } else {
      u8g2_DrawFrame(&osd2, x, y, BTN_W, ROW_H);
    }
    text_to(x + (BTN_W - w) / 2, y + 1, x + BTN_W, labels[i]);
    u8g2_SetDrawColor(&osd2, 1);
  }
}

void ui2_field(uint8_t row, const char *label, const char *text, int cursor, bool focused, const char *note) {
  if (row >= ui2_rows()) return;
  int y = row_y(row);
  u8g2_SetFont(&osd2, F_MAIN);
  int cw = u8g2_GetMaxCharWidth(&osd2);
  text_to(2, y, x_value() - 4, label);
  int x = x_value();
  if (focused) u8g2_DrawFrame(&osd2, x - 2, y - 1, (int) strlen(text) * cw + 4, ROW_H);
  for (int i = 0; text[i]; i++) {
    char c[2] = { text[i], 0 };
    if (focused && i == cursor) {
      u8g2_DrawBox(&osd2, x, y, cw, ROW_H - 2);
      u8g2_SetDrawColor(&osd2, 0);
    }
    text_to(x, y, x + cw + 1, c);
    u8g2_SetDrawColor(&osd2, 1);
    x += cw;
  }
  if (note) {
    u8g2_SetFont(&osd2, F_SMALL);
    text_to(x + 8, y + (ROW_H - 8) / 2 - 1, X_SCROLL - 2, note);
  }
}

void ui2_logo(int x, int y) {
  begin_draw();
  u8g2_DrawBitmap(&osd2, x, y, 7, 16, logo_full);
#if HW_ID == HW_ID_GO
  u8g2_SetDrawColor(&osd2, 0); // the logo is of the Mini: no "mini" on a Go
  u8g2_DrawBox(&osd2, x + 26, y + 11, 13, 5);
  u8g2_SetDrawColor(&osd2, 1);
#endif
}

void ui2_done(void) {
  osd2_flush();
}

// the box of the dialogs: 192 x 40 in the middle of the rows, on tiles (x 4, y 5, 24 x 5), so a
// popup window can show just the box
#define BOX_X 32
#define BOX_Y 40
#define BOX_W 192
#define BOX_H 40
#define BAR_H 14 // the title bar

void ui2_box(const char *line1, const char *line2, bool inverse) {
  begin_draw();
  u8g2_SetDrawColor(&osd2, 0); // with a free margin of 3 pixels: the rows do not touch the frame
  u8g2_DrawBox(&osd2, BOX_X - 3, BOX_Y - 3, BOX_W + 6, BOX_H + 6);
  u8g2_SetDrawColor(&osd2, 1);
  u8g2_DrawFrame(&osd2, BOX_X, BOX_Y, BOX_W, BOX_H);
  u8g2_SetFont(&osd2, F_MAIN);
  int fh = u8g2_GetMaxCharHeight(&osd2);
  if (inverse) {
    // the title on a bar, the text in the middle of the rest
    u8g2_DrawBox(&osd2, BOX_X, BOX_Y, BOX_W, BAR_H);
    u8g2_SetDrawColor(&osd2, 0);
    text_to(BOX_X + 4, BOX_Y + (BAR_H - fh) / 2 + 1, BOX_X + BOX_W - 4, line1);
    u8g2_SetDrawColor(&osd2, 1);
    text_to(BOX_X + 4, BOX_Y + BAR_H + (BOX_H - BAR_H - fh) / 2, BOX_X + BOX_W - 4, line2);
  } else {
    // two lines in the middle of the box
    int gap = (BOX_H - 2 * fh) / 3;
    text_to(BOX_X + 4, BOX_Y + gap, BOX_X + BOX_W - 4, line1);
    text_to(BOX_X + 4, BOX_Y + 2 * gap + fh, BOX_X + BOX_W - 4, line2);
  }
}

void ui2_box_progress(const char *title, uint32_t val, uint32_t max) {
  ui2_box(title, NULL, true);
  int x = BOX_X + 6, y = BOX_Y + BAR_H + (BOX_H - BAR_H - 12) / 2, w = BOX_W - 12;
  u8g2_DrawFrame(&osd2, x, y, w, 12);
  int fill = max ? (int) ((uint64_t) (w - 4) * (val > max ? max : val) / max) : 0;
  u8g2_DrawBox(&osd2, x + 2, y + 2, fill, 8);
}

void ui2_box_popup(void) {
  osd2_flush();
  osd2_popup(BOX_X / 8, BOX_Y / 8, BOX_W / 8, BOX_H / 8, 0, false);
}

// the toast: a box of 2 lines at the top left of the bitmap, shown as a popup in the middle of the
// picture, zoomed
#define TOAST_W 128
#define TOAST_H 32

void ui2_toast(const char *line1, const char *line2) {
  begin_draw();
  u8g2_SetDrawColor(&osd2, 0);
  u8g2_DrawBox(&osd2, 0, 0, TOAST_W, TOAST_H);
  u8g2_SetDrawColor(&osd2, 1);
  u8g2_DrawFrame(&osd2, 0, 0, TOAST_W, TOAST_H);
  u8g2_SetFont(&osd2, F_MAIN);
  int fh = u8g2_GetMaxCharHeight(&osd2);
  int gap = (TOAST_H - 2 * fh) / 3;
  text_to(4, gap, TOAST_W - 4, line1);
  text_to(4, 2 * gap + fh, TOAST_W - 4, line2);
  osd2_flush();
  osd2_popup(0, 0, TOAST_W / 8, TOAST_H / 8, 0, true);
}
