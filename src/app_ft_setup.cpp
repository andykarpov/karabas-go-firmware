#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "FT812.h"
#include "bitmaps.h"
#include "usb_hid_keys.h"
#include "SegaController.h"
#include "ui/settings_items.h"
#include "app_core_browser.h"
#include "app_ft_setup.h"

// The graphic setup of the core browser (S on the FT812 menu): the sections on the left, the
// settings of the chosen one on the right. Every change shows at once (the theme, the clock, the
// buttons), Save writes karabas.ini, Cancel / Esc drop the changes. A new video mode is tried
// first and kept only when confirmed; it falls back after a timeout.
// The setup of the clock, Wi-Fi and gamepads is still in the text menu ("Text setup").

enum { T_SYSTEM, T_AUTOLOAD, T_DISPLAY, T_SOUND, T_THEME, T_PAGES = T_THEME + 1, T_TEXT = T_PAGES, T_SAVE, T_CANCEL, T_COUNT };

// where the keys go
enum { F_TABS, F_PAGE, F_COLOR, F_MODE_TEST };

typedef struct {
  const char *name;
  const uint8_t *ids;
  uint8_t len;
} page_t;

static const uint8_t page_system[] = { SET_DEBUG, SET_DEBUG_HID, SET_BURST, SET_DVI };
static const uint8_t page_autoload[] = { SET_AUTOLOAD, SET_TIMEOUT, SET_CORE };
static const uint8_t page_display[] = { SET_FT, SET_VIDEO_MODE, SET_3D, SET_TIME, SET_DATE, SET_CHAR };
static const uint8_t page_sound[] = { SET_SOUND, SET_CLICK };
static const uint8_t page_theme[] = {
  SET_THEME, SET_COLOR_BG, SET_COLOR_GRADIENT, SET_COLOR_BUTTON, SET_COLOR_ACTIVE,
  SET_COLOR_TEXT, SET_COLOR_TEXT_ACTIVE, SET_COLOR_COPYRIGHT,
};

#define PAGE(name, ids) { name, ids, sizeof(ids) }
static const page_t pages[T_PAGES] = {
  PAGE("System", page_system),
  PAGE("Autoload", page_autoload),
  PAGE("Display", page_display),
  PAGE("Sound", page_sound),
  PAGE("Theme", page_theme),
};

static const char *const tab_names[T_COUNT] = { "System", "Autoload", "Display", "Sound", "Theme", "Text setup", "Save", "Cancel" };

#define TOP 88
#define TAB_X 16
#define TAB_W 160
#define TAB_H 32
#define TAB_STEP 40
#define TAB_GAP 12 // before Text setup
#define PANEL_X (TAB_X + TAB_W + 16)
#define PANEL_MAX_W 560
#define ROW_H 32
#define ROW_STEP 36
#define FONT 28
#define FONT_SMALL 27
#define MODE_TEST_S 10
#define COLOR_STEP 8
#define ERROR_COLOR 0xff4040

static setup_t s;          // the edited copy of hw_setup
static uint8_t focus;      // F_*
static uint8_t tab;
static uint8_t row;
static const char *error;

static uint8_t mode_pick;  // the video mode picked on its row, tried on Enter
static uint8_t mode_cur;   // the video mode the FT812 runs
static uint8_t mode_old;   // the one to fall back to
static bool mode_keep;     // the Keep / Revert button of the test
static ElapsedTimer mode_timer;

static uint8_t color_ch;   // R, G, B in the color editor
static uint32_t color_old;

static uint16_t ft_opt() { return s.ft_3d_buttons ? FT81x_OPT_3D : FT81x_OPT_FLAT; }

static uint16_t panel_w() {
  int w = ft.width() - PANEL_X - 16;
  return w > PANEL_MAX_W ? PANEL_MAX_W : w;
}

static uint8_t cur_id() { return pages[tab].ids[row]; }

static int16_t tab_y(uint8_t i) { return TOP + i * TAB_STEP + (i >= T_TEXT ? TAB_GAP : 0); }

static void draw_tabs() {
  for (uint8_t i = 0; i < T_COUNT; i++) {
    bool chosen = i == tab;
    bool lit = chosen && focus == F_TABS;
    uint32_t bg = lit ? s.color_active : s.color_button;
    uint32_t fg = lit ? s.color_text_active : s.color_text;
    ft.drawButton(TAB_X, tab_y(i), TAB_W, TAB_H, FONT, fg, bg, ft_opt(), tab_names[i]);
    if (chosen && !lit) ft.drawRect(TAB_X, tab_y(i) + 4, 4, TAB_H - 8, 1, s.color_active);
  }
}

static void draw_value(uint8_t id, int16_t y, bool focused) {
  int16_t right = PANEL_X + panel_w() - 16;
  const setting_t *st = &settings[id];
  if (st->kind == SET_KIND_BOOL) {
    bool on = settings_bool(id, s);
    ft.drawToggle(right - 52, y + ROW_H / 2 - 8, 40, FONT_SMALL, s.color_text, s.color_text,
      on ? s.color_active : s.color_copyright, ft_opt(), on, "\xff");
    return;
  }
  if (st->kind == SET_KIND_COLOR) {
    char hex[8];
    settings_value_text(id, s, hex, sizeof(hex));
    ft.drawRect(right - 48, y + 6, 48, ROW_H - 12, 2, *settings_color(id, s));
    ft.drawText(right - 60, y + ROW_H / 2, FONT_SMALL, s.color_text, FT81x_OPT_RIGHTX | FT81x_OPT_CENTERY, hex);
    return;
  }
  char v[24], b[32];
  if (id == SET_VIDEO_MODE) strlcpy(v, FT812::modeName(focused ? mode_pick : s.ft_video_mode), sizeof(v));
  else settings_value_text(id, s, v, sizeof(v));
  if (focused) snprintf(b, sizeof(b), "<  %s  >", v);
  else strlcpy(b, v, sizeof(b));
  ft.drawText(right, y + ROW_H / 2, FONT, s.color_text, FT81x_OPT_RIGHTX | FT81x_OPT_CENTERY, b);
}

static void draw_page() {
  const page_t *p = &pages[tab];
  int16_t w = panel_w();
  ft.drawText(PANEL_X + 16, TOP + TAB_H / 2, 29, s.color_text, FT81x_OPT_CENTERY, p->name);
  ft.drawRect(PANEL_X + 16, TOP + TAB_H + 2, w - 32, 1, 1, s.color_copyright);
  for (uint8_t i = 0; i < p->len; i++) {
    int16_t y = TOP + 40 + i * ROW_STEP;
    bool focused = focus == F_PAGE && i == row;
    if (focused) {
      ft.drawRect(PANEL_X, y, w, ROW_H, 4, s.color_button);
      ft.drawRect(PANEL_X, y + 4, 4, ROW_H - 8, 1, s.color_active);
    }
    const char *label = settings[p->ids[i]].label;
    if (p->ids[i] == SET_AUTOLOAD) label = "Autoload";
    ft.drawText(PANEL_X + 16, y + ROW_H / 2, FONT, s.color_text, FT81x_OPT_CENTERY, label);
    draw_value(p->ids[i], y, focused);
  }
}

// the editor of one color: R, G, B sliders
static void draw_color() {
  uint8_t id = cur_id();
  uint32_t c = *settings_color(id, s);
  int16_t w = panel_w();
  char b[40];
  snprintf(b, sizeof(b), "Theme > %s", settings[id].label);
  ft.drawText(PANEL_X + 16, TOP + TAB_H / 2, 29, s.color_text, FT81x_OPT_CENTERY, b);
  ft.drawRect(PANEL_X + 16, TOP + TAB_H + 2, w - 32, 1, 1, s.color_copyright);
  ft.drawRect(PANEL_X + 16, TOP + 48, 96, 64, 4, c);
  settings_value_text(id, s, b, sizeof(b));
  ft.drawText(PANEL_X + 128, TOP + 80, 30, s.color_text, FT81x_OPT_CENTERY, b);

  static const char *const names[3] = { "R", "G", "B" };
  static const uint32_t fills[3] = { 0xd02020, 0x20b020, 0x3050ff };
  for (uint8_t i = 0; i < 3; i++) {
    int16_t y = TOP + 128 + i * ROW_STEP;
    uint8_t v = (c >> (16 - 8 * i)) & 0xff;
    if (i == color_ch) {
      ft.drawRect(PANEL_X, y, w, ROW_H, 4, s.color_button);
      ft.drawRect(PANEL_X, y + 4, 4, ROW_H - 8, 1, s.color_active);
    }
    ft.drawText(PANEL_X + 16, y + ROW_H / 2, FONT, s.color_text, FT81x_OPT_CENTERY, names[i]);
    ft.drawSlider(PANEL_X + 48, y + ROW_H / 2 - 5, w - 48 - 72, 10, fills[i], s.color_text, s.color_copyright, ft_opt(), v, 255);
    snprintf(b, sizeof(b), "%u", v);
    ft.drawText(PANEL_X + w - 16, y + ROW_H / 2, FONT, s.color_text, FT81x_OPT_RIGHTX | FT81x_OPT_CENTERY, b);
  }
}

static void draw_mode_test() {
  int16_t w = 400, h = 160;
  int16_t x = (ft.width() - w) / 2, y = (ft.height() - h) / 2;
  ft.drawRectAlpha(0, 0, ft.width(), ft.height(), 0, 0x000000, 160);
  ft.drawRect(x, y, w, h, 8, s.color_button);
  char b[40];
  snprintf(b, sizeof(b), "Keep %s?", FT812::modeName(mode_cur));
  ft.drawText(x + w / 2, y + 30, 29, s.color_text, FT81x_OPT_CENTER, b);
  uint32_t left = mode_timer.elapsed() < MODE_TEST_S * 1000 ? (MODE_TEST_S * 1000 - mode_timer.elapsed() + 999) / 1000 : 0;
  snprintf(b, sizeof(b), "Going back in %lu s", (unsigned long) left);
  ft.drawText(x + w / 2, y + 66, FONT_SMALL, s.color_copyright, FT81x_OPT_CENTER, b);
  ft.drawButton(x + 40, y + h - 56, 140, TAB_H, FONT, mode_keep ? s.color_text_active : s.color_text,
    mode_keep ? s.color_active : s.color_bg, ft_opt(), "Keep");
  ft.drawButton(x + w - 180, y + h - 56, 140, TAB_H, FONT, !mode_keep ? s.color_text_active : s.color_text,
    !mode_keep ? s.color_active : s.color_bg, ft_opt(), "Revert");
}

static void draw_footer() {
  const char *hint = "";
  const char *keys = "";
  switch (focus) {
    case F_TABS:
      hint = tab == T_TEXT ? "Clock, Wi-Fi, gamepads (drops the changes)" :
             tab == T_SAVE ? "Write the settings to karabas.ini" :
             tab == T_CANCEL ? "Drop the changes" : "";
      keys = "Up/Down: section   Enter: open   Esc: cancel";
      break;
    case F_PAGE:
      hint = cur_id() == SET_VIDEO_MODE ? "Enter: try the mode" : settings[cur_id()].hint;
      keys = "Up/Down: select   Left/Right: change   Esc: back";
      break;
    case F_COLOR:
      hint = "Shift: fine steps";
      keys = "Up/Down: R G B  Left/Right: change  Enter: OK  Esc: undo";
      break;
    case F_MODE_TEST:
      keys = "Left/Right: choose   Enter: OK";
      break;
  }
  ft.drawText(TAB_X, ft.height() - 40, FONT_SMALL, error ? ERROR_COLOR : s.color_copyright, FT81x_OPT_CENTERY, error ? error : hint);
  ft.drawText(TAB_X, ft.height() - 20, FONT_SMALL, s.color_copyright, FT81x_OPT_CENTERY, keys);
}

void app_ft_setup_draw() {
  if (!has_ft) return;
  ft.beginDisplayList();
  app_core_browser_ft_background(s);
  bool page = tab < T_PAGES;
  // the picture only where it does not cover the settings
  bool show_char = !page || PANEL_X + panel_w() < ft.width() - BG_WIDTH - 8;
  app_core_browser_ft_decor(s, show_char);
  draw_tabs();
  if (focus == F_COLOR) draw_color();
  else if (page) draw_page();
  draw_footer();
  if (focus == F_MODE_TEST) draw_mode_test();
  ft.swapScreen();
}

static void set_mode(uint8_t m) {
  if (ft.init(m)) {
    mode_cur = m;
  } else {
    d_printf("FT81x did not come up in video mode %u", m); d_println();
    ft.init(mode_cur);
  }
  app_core_browser_ft_load();
}

void app_ft_setup_overlay() {
  s = hw_setup;
  settings_begin(s);
  focus = F_TABS;
  tab = T_SYSTEM;
  row = 0;
  error = nullptr;
  mode_cur = hw_setup.ft_video_mode;
  mode_pick = s.ft_video_mode;
  app_ft_setup_draw();
}

static void leave() {
  if (mode_cur != hw_setup.ft_video_mode) set_mode(hw_setup.ft_video_mode);
  osd_state = osd_return_state;
}

void app_ft_setup_tick() {
  if (focus == F_MODE_TEST && mode_timer.elapsed() >= MODE_TEST_S * 1000) {
    set_mode(mode_old);
    focus = F_PAGE;
    app_ft_setup_draw();
  }
}

static void open_tab() {
  switch (tab) {
    case T_TEXT:
      has_ft = false;
      ft.vga(false);
      ft.spi(false);
      osd_state = state_setup;
      break;
    case T_SAVE:
      if (save_setup(s)) { leave(); return; }
      error = "Can't save karabas.ini";
      break;
    case T_CANCEL:
      leave();
      break;
    default:
      focus = F_PAGE;
      row = 0;
      mode_pick = s.ft_video_mode;
  }
}

static void change(int d) {
  uint8_t id = cur_id();
  if (id == SET_VIDEO_MODE) {
    uint8_t n = FT812::modeCount();
    mode_pick = (mode_pick + d + n) % n;
    return;
  }
  settings_change(id, s, d);
  if (id == SET_SOUND && s.ft_sound > 0) app_core_browser_ft_sound(s.ft_sound, false);
}

static void enter_row() {
  uint8_t id = cur_id();
  if (settings[id].kind == SET_KIND_COLOR) {
    focus = F_COLOR;
    color_ch = 0;
    color_old = *settings_color(id, s);
  } else if (id == SET_VIDEO_MODE) {
    if (mode_pick == mode_cur) return;
    mode_old = mode_cur;
    set_mode(mode_pick);
    if (mode_cur != mode_pick) {
      error = "The video mode did not start";
      return;
    }
    focus = F_MODE_TEST;
    mode_keep = false;
    mode_timer.reset();
  } else {
    change(1);
  }
}

static void change_color(int d) {
  uint32_t *c = settings_color(cur_id(), s);
  uint8_t shift = 16 - 8 * color_ch;
  int v = ((*c >> shift) & 0xff) + d;
  v = v < 0 ? 0 : v > 255 ? 255 : v;
  *c = (*c & ~(0xfful << shift)) | ((uint32_t) v << shift);
}

void app_ft_setup_on_keyboard() {
  uint8_t key = usb_keyboard_report.keycode[0];
  bool up = key == KEY_UP || (joyL & SC_BTN_UP) || (joyR & SC_BTN_UP);
  bool down = key == KEY_DOWN || (joyL & SC_BTN_DOWN) || (joyR & SC_BTN_DOWN);
  bool left = key == KEY_LEFT || (joyL & SC_BTN_LEFT) || (joyR & SC_BTN_LEFT);
  bool right = key == KEY_RIGHT || (joyL & SC_BTN_RIGHT) || (joyR & SC_BTN_RIGHT);
  bool enter = key == KEY_ENTER || key == KEY_KPENTER || (joyL & SC_BTN_A) || (joyR & SC_BTN_A) || (joyL & SC_BTN_B) || (joyR & SC_BTN_B);
  bool esc = key == KEY_ESC || (joyL & SC_BTN_C) || (joyR & SC_BTN_C);
  bool back = esc || key == KEY_BACKSPACE;
  bool shift = usb_keyboard_report.modifier & (KEY_MOD_LSHIFT | KEY_MOD_RSHIFT);
  if (!(up || down || left || right || enter || back || key == KEY_TAB)) return;
  error = nullptr;

  switch (focus) {
    case F_TABS:
      if (up) tab = (tab + T_COUNT - 1) % T_COUNT;
      else if (down || key == KEY_TAB) tab = (tab + 1) % T_COUNT;
      else if (enter || (right && tab < T_PAGES)) open_tab();
      else if (esc) leave();
      break;
    case F_PAGE: {
      uint8_t n = pages[tab].len;
      if (up) row = (row + n - 1) % n;
      else if (down) row = (row + 1) % n;
      else if (key == KEY_TAB) { tab = (tab + 1) % T_PAGES; row = 0; }
      else if (left || right) change(left ? -1 : 1);
      else if (enter) enter_row();
      else if (back) focus = F_TABS;
      if (!up && !down) break;
      mode_pick = s.ft_video_mode;
      break;
    }
    case F_COLOR:
      if (up) color_ch = (color_ch + 2) % 3;
      else if (down || key == KEY_TAB) color_ch = (color_ch + 1) % 3;
      else if (left || right) change_color((left ? -1 : 1) * (shift ? 1 : COLOR_STEP));
      else if (enter) focus = F_PAGE;
      else if (back) { *settings_color(cur_id(), s) = color_old; focus = F_PAGE; }
      break;
    case F_MODE_TEST:
      if (left || right) mode_keep = !mode_keep;
      else if (enter && mode_keep) { s.ft_video_mode = mode_cur; focus = F_PAGE; }
      else if (enter || esc) { set_mode(mode_old); focus = F_PAGE; }
      break;
  }

  if (osd_state != state_ft_setup) return; // left the setup
  if (s.ft_click) ft.playSound();
  app_ft_setup_draw();
}
