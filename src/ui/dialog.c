#include "main.h"
#include "ui/dialog.h"
#include "ui/osd_ui.h"
#include "ui/ui_draw.h"

// the dialogs on the text OSD of link v1: the cells and the popup mode of lib/osd, as the callers
// drew them before

void dialog_toast(const char *title, const char *text, uint8_t item) {
  osd_clear();
  osd_logo(0, 0, HW_ID);
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_set_pos(0, 5);
  osd_print(title);
  osd_set_pos(0,6);
  osd_print(text);
  osd_update();
  ui_popup_start(item);
}

void dialog_progress(const char *title, uint32_t val, uint32_t max) {
  osd_progress_popup(title, val, max);
  osd_update();
}

void dialog_loading(uint32_t val, uint32_t max) {
  dialog_progress("Loading...", val, max);
}

void dialog_hide(void) {
  osd_hide_popup();
}

void dialog_load_screen(void) {
  osd_clear();
  osd_logo(0,0, HW_ID);
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_set_pos(0,5);
  osd_print("ROM ");
  osd_update();
  osd_show_popup(0);
}

void dialog_load_rom(uint32_t idx) {
  osd_set_pos(0, 5 + idx);
  osd_print("ROM ");
  osd_update();
}

void dialog_load_rom_sent(uint32_t idx, uint32_t sent) {
  osd_set_pos(4, 5 + idx);
  osd_printf("%lu: ", (unsigned long) idx + 1);
  char b[40];
  sprintf(b, "%05lu", (unsigned long) sent); osd_print(b); osd_print(" ");
  osd_update();
}

void dialog_load_rom_missing(uint32_t idx) {
  osd_set_pos(4, 5 + idx);
  osd_printf("%lu:  NO FILE", (unsigned long) idx + 1);
  osd_update();
}

void dialog_box(dialog_kind_t kind, const char *title, const char *text) {
  switch (kind) {
    case DIALOG_ERROR: ui_box(title, OSD_COLOR_RED_I, text, OSD_COLOR_RED_I); break;
    case DIALOG_BUSY: ui_box(title, OSD_COLOR_GREEN_I, text, OSD_COLOR_MAGENTA_I); break;
    case DIALOG_ASK: ui_box(title, OSD_COLOR_YELLOW_I, text, OSD_COLOR_WHITE); break;
  }
  osd_update();
}

void dialog_box_clear(void) {
  ui_box_clear();
  osd_update();
}
