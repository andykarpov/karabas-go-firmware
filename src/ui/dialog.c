#include "main.h"
#include "ui/dialog.h"
#include "ui/osd_ui.h"
#include "ui/ui_draw.h"
#include "ui/osd2.h"
#include "ui/ui2.h"

// the dialogs on the text OSD of link v1: the cells and the popup mode of lib/osd, as the callers
// drew them before

void dialog_toast(const char *title, const char *text, uint8_t item) {
  if (osd2_active()) {
    ui2_toast(title, text);
    ui_popup_start(item); // hidden after 500 ms (the popup off ends the window)
    return;
  }
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
  if (osd2_active()) {
    ui2_box_progress(title, val, max);
    if (is_osd) osd2_flush(); else ui2_box_popup();
    return;
  }
  osd_progress_popup(title, val, max);
  osd_update();
}

void dialog_loading(uint32_t val, uint32_t max) {
  dialog_progress("Loading...", val, max);
}

void dialog_hide(void) {
  if (osd2_active()) { osd2_popup(0, 0, 0, 0, 0, false); return; }
  osd_hide_popup();
}

static uint32_t load_rom_idx = 0; // v2: the ROM of the load screen

void dialog_load_screen(void) {
  if (osd2_active()) {
    load_rom_idx = 0;
    ui2_box("Loading ROMs", "ROM 1", true);
    ui2_box_popup();
    return;
  }
  osd_clear();
  osd_logo(0,0, HW_ID);
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_set_pos(0,5);
  osd_print("ROM ");
  osd_update();
  osd_show_popup(0);
}

void dialog_load_rom(uint32_t idx) {
  if (osd2_active()) {
    char t[24];
    load_rom_idx = idx;
    snprintf(t, sizeof(t), "ROM %lu", (unsigned long) idx + 1);
    ui2_box("Loading ROMs", t, true);
    if (is_osd) osd2_flush(); else ui2_box_popup();
    return;
  }
  osd_set_pos(0, 5 + idx);
  osd_print("ROM ");
  osd_update();
}

void dialog_load_rom_sent(uint32_t idx, uint32_t sent) {
  if (osd2_active()) {
    char t[32];
    snprintf(t, sizeof(t), "ROM %lu: %lu bytes", (unsigned long) idx + 1, (unsigned long) sent);
    ui2_box("Loading ROMs", t, true);
    if (is_osd) osd2_flush(); else ui2_box_popup();
    return;
  }
  osd_set_pos(4, 5 + idx);
  osd_printf("%lu: ", (unsigned long) idx + 1);
  char b[40];
  sprintf(b, "%05lu", (unsigned long) sent); osd_print(b); osd_print(" ");
  osd_update();
}

void dialog_load_rom_missing(uint32_t idx) {
  if (osd2_active()) {
    char t[32];
    snprintf(t, sizeof(t), "ROM %lu: no file", (unsigned long) idx + 1);
    ui2_box("Loading ROMs", t, true);
    if (is_osd) osd2_flush(); else ui2_box_popup();
    return;
  }
  osd_set_pos(4, 5 + idx);
  osd_printf("%lu:  NO FILE", (unsigned long) idx + 1);
  osd_update();
}

void dialog_box(dialog_kind_t kind, const char *title, const char *text) {
  if (osd2_active()) {
    ui2_box(title, text, kind != DIALOG_BUSY);
    osd2_flush();
    return;
  }
  switch (kind) {
    case DIALOG_ERROR: ui_box(title, OSD_COLOR_RED_I, text, OSD_COLOR_RED_I); break;
    case DIALOG_BUSY: ui_box(title, OSD_COLOR_GREEN_I, text, OSD_COLOR_MAGENTA_I); break;
    case DIALOG_ASK: ui_box(title, OSD_COLOR_YELLOW_I, text, OSD_COLOR_WHITE); break;
  }
  osd_update();
}

void dialog_box_clear(void) {
  if (osd2_active()) return; // the screen is drawn again after it
  ui_box_clear();
  osd_update();
}
