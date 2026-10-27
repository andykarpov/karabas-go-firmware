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
  if (line1) { osd_set_pos(1, UI_ROW_HINT); osd_print(line1); }
  if (line2) { osd_set_pos(1, UI_ROW_HINT + 1); osd_print(line2); }
}

void ui_row(uint8_t x, uint8_t y, const char *text, bool selected) {
  osd_set_color(selected ? OSD_COLOR_BLACK : OSD_COLOR_WHITE, selected ? OSD_COLOR_WHITE : OSD_COLOR_BLACK);
  osd_set_pos(x, y);
  for (uint8_t i = x; i < OSD_SIZE_X; i++) osd_putc(*text ? (uint8_t) *text++ : ' ');
}

void ui_hotkey(const char *key, const char *rest) {
  osd_set_color(OSD_COLOR_CYAN_I, OSD_COLOR_FLASH); osd_print(key);
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK); osd_print(rest);
}
