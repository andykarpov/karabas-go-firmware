#include "main.h"
#include "ui/ui_draw.h"

void ui_screen(const char *title) {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_clear();
  osd_header(core.build, core.id, HW_ID);
  print_time();
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_set_pos(0, UI_ROW_TITLE);
  osd_print(title);
  osd_line(UI_ROW_TITLE + 1);
}

void ui_hint(const char *line1, const char *line2) {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_line(UI_ROW_HINT_LINE);
  ui_row(0, UI_ROW_HINT, "", false);
  ui_row(0, UI_ROW_HINT + 1, "", false);
  if (line1) { osd_set_pos(0, UI_ROW_HINT); osd_print(line1); }
  if (line2) { osd_set_pos(0, UI_ROW_HINT + 1); osd_print(line2); }
}

void ui_row(uint8_t x, uint8_t y, const char *text, bool selected) {
  ui_row_ink(x, y, text, selected, OSD_COLOR_WHITE);
}

void ui_row_ink(uint8_t x, uint8_t y, const char *text, bool selected, uint8_t ink) {
  osd_set_color(selected ? OSD_COLOR_BLACK : ink, selected ? ink : OSD_COLOR_BLACK);
  osd_set_pos(x, y);
  for (uint8_t i = x; i < OSD_SIZE_X; i++) osd_putc(*text ? (uint8_t) *text++ : ' ');
}

void ui_hotkey(const char *key, const char *rest) {
  osd_set_color(OSD_COLOR_CYAN_I, OSD_COLOR_FLASH); osd_print(key);
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK); osd_print(rest);
}

void ui_clear_rows(uint8_t y1, uint8_t y2) {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_fill(0, y1, UI_COL_LAST, y2, ' ');
}

void ui_clear_body(void) { ui_clear_rows(UI_ROW_BODY, UI_ROW_BODY_LAST); }

// the box: a frame around UI_BOX_TEXT x 2 chars
#define BOX_X1 8
#define BOX_Y1 8
#define BOX_X2 (BOX_X1 + UI_BOX_TEXT + 1)
#define BOX_Y2 (BOX_Y1 + 4)

void ui_box(const char *line1, uint8_t color1, const char *line2, uint8_t color2) {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_frame(BOX_X1, BOX_Y1, BOX_X2, BOX_Y2);
  osd_fill(BOX_X1 + 1, BOX_Y1 + 1, BOX_X2 - 1, BOX_Y2 - 1, ' ');
  osd_set_color(color1, OSD_COLOR_BLACK);
  osd_set_pos(BOX_X1 + 1, BOX_Y1 + 1); osd_printf("%.*s", UI_BOX_TEXT, line1);
  osd_set_color(color2, OSD_COLOR_BLACK);
  osd_set_pos(BOX_X1 + 1, BOX_Y1 + 2); osd_printf("%.*s", UI_BOX_TEXT, line2);
}

void ui_box_clear(void) {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_fill(BOX_X1, BOX_Y1, BOX_X2, BOX_Y2, ' ');
}
