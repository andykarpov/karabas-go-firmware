#include "main.h"
#include "ui/ui2.h"
#include "ui/osd2.h"

#define F_MAIN u8g2_font_6x12_t_cyrillic
#define F_SMALL u8g2_font_5x8_t_cyrillic

#define Y_ROWS 13
#define ROW_H 12
#define X_VALUE 86           // 14 characters of the label
#define X_SCROLL 252
#define Y_HINT 112

// the logo of the header: four bricks and KARABAS (as the colour bricks of the text OSD)
static void logo(void) {
  for (uint8_t i = 0; i < 4; i++) u8g2_DrawBox(&osd2, 1 + i * 5, 4, 4, 4);
  u8g2_SetFont(&osd2, F_MAIN);
  u8g2_DrawStr(&osd2, 23, 0, "Karabas");
}

static void clock_text(char *out, size_t size) {
  snprintf(out, size, "%02u:%02u:%02u", rtc_get_hour(), rtc_get_minute(), rtc_get_second());
}

void ui2_screen(const char *title) {
  osd2_begin();
  u8g2_ClearBuffer(&osd2);
  u8g2_SetFontPosTop(&osd2);
  u8g2_SetFontMode(&osd2, 1);
  u8g2_SetDrawColor(&osd2, 1);
  logo();
  u8g2_SetFont(&osd2, F_MAIN);
  // the title between the logo and the clock, cut to fit
  char t[40];
  snprintf(t, sizeof(t), "%s", title ? title : core.name);
  int x = 23 + 7 * 6 + 6;
  int room = OSD2_W - 50 - x;
  while (t[0] && u8g2_GetUTF8Width(&osd2, t) > room) t[strlen(t) - 1] = '\0';
  u8g2_DrawUTF8(&osd2, x, 0, t);
  char c[12];
  clock_text(c, sizeof(c));
  u8g2_DrawStr(&osd2, OSD2_W - 1 - u8g2_GetStrWidth(&osd2, c), 0, c);
  u8g2_DrawHLine(&osd2, 0, 12, OSD2_W);
  u8g2_DrawHLine(&osd2, 0, 110, OSD2_W);
}

void ui2_clock(void) {
  if (!osd2_active()) return;
  char c[12];
  clock_text(c, sizeof(c));
  u8g2_SetFont(&osd2, F_MAIN);
  u8g2_SetFontPosTop(&osd2);
  int w = u8g2_GetStrWidth(&osd2, c);
  u8g2_SetDrawColor(&osd2, 0);
  u8g2_DrawBox(&osd2, OSD2_W - 1 - w, 0, w, 12);
  u8g2_SetDrawColor(&osd2, 1);
  u8g2_DrawStr(&osd2, OSD2_W - 1 - w, 0, c);
  osd2_flush();
}

// a string drawn from x, cut at x_end
static void text_to(int x, int y, int x_end, const char *s) {
  char t[64];
  snprintf(t, sizeof(t), "%s", s ? s : "");
  while (t[0] && x + u8g2_GetUTF8Width(&osd2, t) > x_end) {
    // cut a whole UTF-8 character
    size_t n = strlen(t);
    do { n--; } while (n > 0 && (t[n] & 0xC0) == 0x80);
    t[n] = '\0';
  }
  u8g2_DrawUTF8(&osd2, x, y, t);
}

static int row_y(uint8_t row) { return Y_ROWS + row * ROW_H; }

static void row_bar(uint8_t row, uint8_t flags) {
  int y = row_y(row);
  if (flags & UI2_SEL) {
    u8g2_SetDrawColor(&osd2, 1);
    u8g2_DrawBox(&osd2, 0, y, X_SCROLL - 1, ROW_H);
    u8g2_SetDrawColor(&osd2, 0);
  } else {
    u8g2_SetDrawColor(&osd2, 1);
  }
}

void ui2_row(uint8_t row, const char *label, const char *value, const char *hotkey, uint8_t flags) {
  if (row >= UI2_ROWS) return;
  int y = row_y(row);
  row_bar(row, flags);
  int hk_x = X_SCROLL - 2;
  if (hotkey && hotkey[0]) {
    u8g2_SetFont(&osd2, F_SMALL);
    int w = u8g2_GetUTF8Width(&osd2, hotkey);
    hk_x = X_SCROLL - 3 - w;
    u8g2_DrawUTF8(&osd2, hk_x, y + 2, hotkey);
  }
  // a disabled row in the small font: readable, but plainly not the others
  bool dimmed = flags & UI2_DIM;
  u8g2_SetFont(&osd2, dimmed ? F_SMALL : F_MAIN);
  int ty = dimmed ? y + 2 : y;
  text_to(2, ty, X_VALUE - 4, label);
  if (flags & UI2_ARROW) {
    u8g2_DrawStr(&osd2, X_VALUE, ty, ">");
  } else if (value) {
    text_to(X_VALUE, ty, hk_x - 4, value);
  }
  u8g2_SetDrawColor(&osd2, 1);
}

void ui2_text(uint8_t row, const char *text, uint8_t flags) {
  if (row >= UI2_ROWS) return;
  int y = row_y(row);
  row_bar(row, flags);
  u8g2_SetFont(&osd2, (flags & UI2_DIM) ? F_SMALL : F_MAIN);
  text_to(2, (flags & UI2_DIM) ? y + 2 : y, X_SCROLL - 2, text);
  u8g2_SetDrawColor(&osd2, 1);
  if (flags & UI2_TITLE) u8g2_DrawHLine(&osd2, 0, y + ROW_H - 1, X_SCROLL - 1);
}

void ui2_sep(uint8_t row) {
  if (row >= UI2_ROWS) return;
  int y = row_y(row) + ROW_H / 2;
  for (int x = 2; x < X_SCROLL - 2; x += 2) u8g2_DrawPixel(&osd2, x, y);
}

void ui2_scrollbar(uint16_t top, uint16_t total) {
  if (total <= UI2_ROWS) return;
  int y0 = Y_ROWS, h = UI2_ROWS * ROW_H;
  for (int y = y0; y < y0 + h; y += 2) u8g2_DrawPixel(&osd2, X_SCROLL + 1, y); // the track, dotted
  int th = h * UI2_ROWS / total;
  if (th < 4) th = 4;
  int ty = y0 + (h - th) * top / (total - UI2_ROWS);
  u8g2_DrawBox(&osd2, X_SCROLL, ty, 3, th);
}

void ui2_hint(const char *line1, const char *line2) {
  u8g2_SetDrawColor(&osd2, 0);
  u8g2_DrawBox(&osd2, 0, Y_HINT - 1, OSD2_W, OSD2_H - Y_HINT + 1);
  u8g2_SetDrawColor(&osd2, 1);
  u8g2_SetFont(&osd2, F_SMALL);
  if (line1) text_to(1, Y_HINT, OSD2_W - 1, line1);
  if (line2) text_to(1, Y_HINT + 8, OSD2_W - 1, line2);
}

void ui2_done(void) {
  osd2_flush();
}

// the box of the dialogs: 192 x 40 in the middle of the rows
#define BOX_X 32
#define BOX_Y 38
#define BOX_W 192
#define BOX_H 40

void ui2_box(const char *line1, const char *line2, bool inverse) {
  u8g2_SetDrawColor(&osd2, 0);
  u8g2_DrawBox(&osd2, BOX_X, BOX_Y, BOX_W, BOX_H);
  u8g2_SetDrawColor(&osd2, 1);
  u8g2_DrawFrame(&osd2, BOX_X, BOX_Y, BOX_W, BOX_H);
  u8g2_SetFont(&osd2, F_MAIN);
  if (inverse) {
    u8g2_DrawBox(&osd2, BOX_X, BOX_Y, BOX_W, 13);
    u8g2_SetDrawColor(&osd2, 0);
  }
  text_to(BOX_X + 4, BOX_Y + 1, BOX_X + BOX_W - 4, line1);
  u8g2_SetDrawColor(&osd2, 1);
  text_to(BOX_X + 4, BOX_Y + 16, BOX_X + BOX_W - 4, line2);
}

void ui2_box_progress(const char *title, uint32_t val, uint32_t max) {
  ui2_box(title, NULL, true);
  int x = BOX_X + 6, y = BOX_Y + 20, w = BOX_W - 12;
  u8g2_DrawFrame(&osd2, x, y, w, 12);
  int fill = max ? (int) ((uint64_t) (w - 4) * (val > max ? max : val) / max) : 0;
  u8g2_DrawBox(&osd2, x + 2, y + 2, fill, 8);
}
