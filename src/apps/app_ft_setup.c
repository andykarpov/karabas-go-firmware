#include "config.h"
#include "types.h"
#include "main.h"
#include "ft812.h"
#include "ui/bitmaps.h"
#include "input/usb_hid_keys.h"
#include "input/sega.h"
#include "ui/settings_items.h"
#include "apps/app_core_browser.h"
#include "ui/ft_mouse.h"
#include "apps/app_ft_pages.h"
#include "apps/app_ft_setup.h"

// The graphic setup of the core browser (S on the FT812 menu): the sections on the left, the
// settings of the chosen one on the right. Every change shows at once (the theme, the clock, the
// buttons), Save writes karabas.ini, Cancel drops the changes (Esc asks first). A new video mode
// is tried first and kept only when confirmed; it falls back after a timeout.
// The pages after the settings (Clock, Wi-Fi, Gamepad) are their own modules (ui/ft_page.h);
// their changes are done at once, as in the text menu.
// The mouse: a click on a section opens it, on a setting picks it (and does Enter when it is
// picked), on a toggle or an arrow changes it; the wheel changes the setting under the pointer
// or the section; the sliders of a color follow a drag; the right button is Esc.

// T_PAGES: the settings pages; then the pages of their own modules
enum { T_SYSTEM, T_AUTOLOAD, T_DISPLAY, T_SOUND, T_THEME, T_PAGES, T_CLOCK = T_PAGES, T_WIFI, T_PAD, T_SAVE, T_CANCEL, T_COUNT };

// where the keys go
enum { F_TABS, F_PAGE, F_COLOR, F_DIALOG, F_CUSTOM };

// the dialogs: the left button does it, the right one (the default) is the safe way back
enum { D_MODE, D_DROP };

// the clickable areas: the base plus the index
enum { HIT_TAB = 100, HIT_ROW = 200, HIT_TOGGLE = 300, HIT_LESS = 400, HIT_MORE = 500,
  HIT_CHAN = 600, HIT_SLIDER = 700, HIT_DLG_LEFT = 800, HIT_DLG_RIGHT, HIT_PANEL = 900 };

typedef struct {
  const char *name;
  const uint8_t *ids;
  uint8_t len;
} page_t;

static const uint8_t page_system[] = { SET_DEBUG, SET_DVI, SET_MATRIX, SET_MATRIX_BRIGHTNESS, SET_MX_EFFECTS };
static const uint8_t page_autoload[] = { SET_AUTOLOAD, SET_TIMEOUT, SET_CORE };
static const uint8_t page_display[] = { SET_FT, SET_VIDEO_MODE, SET_3D, SET_TIME, SET_DATE, SET_CHAR, SET_MOUSE_SPEED };
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

static const ft_page_t *const custom_pages[T_SAVE - T_PAGES] = { &ft_page_clock, &ft_page_wifi, &ft_page_pad };

static const char *const tab_names[T_COUNT] = { "System", "Autoload", "Display", "Sound", "Theme", "Clock", "Wi-Fi", "Gamepad", "Save", "Cancel" };

#define TOP 88
#define TAB_X 16
#define TAB_W 160
#define TAB_H 32 // at most, as TAB_STEP: less where the tabs do not fit (640x480)
#define TAB_STEP 40
#define FOOTER_H 56
#define TAB_GAP 12 // before Save
#define PANEL_X (TAB_X + TAB_W + 16)
#define PANEL_MAX_W 560
#define ROW_H 32
#define ROW_STEP 36
#define FONT 28
#define FONT_SMALL 27
#define MODE_TEST_S 10
#define COLOR_STEP 8
#define ERROR_COLOR 0xff4040
#define VALUE_W 200 // '<', the value of a choice, '>'

static setup_t s;          // the edited copy of hw_setup
static uint8_t focus;      // F_*
static uint8_t tab;
static uint8_t row;
static const char *error;

static uint8_t mode_pick;  // the video mode picked on its row, tried on Enter
static uint8_t mode_cur;   // the video mode the FT812 runs
static uint8_t mode_old;   // the one to fall back to
static uint32_t mode_timer = 0;

static uint8_t dialog;     // D_*
static bool dialog_left;   // the left button is chosen

static uint8_t color_ch;   // R, G, B in the color editor
static uint32_t color_old;

static int16_t hover = FT_HIT_NONE; // under the pointer at the last draw
static bool dragging;      // a slider follows the pointer
static hid_keyboard_report_t prev_kbd; // for the keys of the custom pages
static const ft_page_t *shown;  // the custom page whose tab is chosen (show / hide)

static uint16_t ft_opt() { return s.ft_3d_buttons ? FT81x_OPT_3D : FT81x_OPT_FLAT; }

static uint16_t panel_w() {
  int w = ft_width() - PANEL_X - 16;
  return w > PANEL_MAX_W ? PANEL_MAX_W : w;
}

static bool mx_page; // System > Matrix effects is open instead of the System page

static const page_t *cur_page() {
  static page_t mx;
  if (!mx_page) return &pages[tab];
  mx = (page_t) { "System > Matrix effects", settings_mx_ids, settings_mx_len };
  return &mx;
}

static uint8_t cur_id() { return cur_page()->ids[row]; }

// the row of a setting on the page of the tab
static uint8_t page_row(uint8_t id) {
  for (uint8_t i = 0; i < pages[tab].len; i++) if (pages[tab].ids[i] == id) return i;
  return 0;
}

static const ft_page_t *custom_page() { return tab >= T_PAGES && tab < T_SAVE ? custom_pages[tab - T_PAGES] : NULL; }

static int16_t tab_step() {
  int16_t step = (ft_height() - FOOTER_H - TOP - TAB_GAP) / T_COUNT;
  return step > TAB_STEP ? TAB_STEP : step;
}

static int16_t tab_h() { return tab_step() - (TAB_STEP - TAB_H); }

static int16_t tab_y(uint8_t i) { return TOP + i * tab_step() + (i >= T_SAVE ? TAB_GAP : 0); }

static void draw_tabs() {
  for (uint8_t i = 0; i < T_COUNT; i++) {
    bool chosen = i == tab;
    bool lit = chosen && focus == F_TABS;
    uint32_t bg = lit ? s.color_active : s.color_button;
    uint32_t fg = lit ? s.color_text_active : s.color_text;
    int16_t h = tab_h();
    if (ft_hit_add(TAB_X, tab_y(i), TAB_W, h, HIT_TAB + i)) {
      ft_draw_rect(TAB_X, tab_y(i), TAB_W, h, 2, s.color_active); // hovered: a 2 px frame (the corner radius)
    }
    ft_draw_button(TAB_X, tab_y(i), TAB_W, h, h < TAB_H ? FONT_SMALL : FONT, fg, bg, ft_opt(), tab_names[i]);
    if (chosen && !lit) ft_page_marker(TAB_X, tab_y(i), h, s.color_active, ft_opt());
  }
}

// arrows: the arrows of a choice (focused or hovered)
static void draw_value(uint8_t id, int16_t y, bool focused, bool arrows) {
  int16_t right = PANEL_X + panel_w() - 16;
  const setting_t *st = &settings[id];
  if (st->kind == SET_KIND_BOOL) {
    bool on = settings_bool(id, &s);
    ft_draw_toggle(right - 52, y + ROW_H / 2 - 8, 40, FONT_SMALL, s.color_text, s.color_text,
      on ? s.color_active : s.color_copyright, ft_opt(), on, "\xff");
    return;
  }
  if (st->kind == SET_KIND_COLOR) {
    char hex[8];
    settings_value_text(id, &s, hex, sizeof(hex));
    ft_draw_rect(right - 48, y + 6, 48, ROW_H - 12, 2, *settings_color(id, &s));
    ft_draw_text(right - 60, y + ROW_H / 2, FONT_SMALL, s.color_text, FT81x_OPT_RIGHTX | FT81x_OPT_CENTERY, hex);
    return;
  }
  if (st->kind == SET_KIND_PAGE) {
    ft_draw_text(right - 12, y + ROW_H / 2, FONT, s.color_text, FT81x_OPT_CENTER, ">");
    return;
  }
  char v[14]; // a long core name is shortened to fit between the arrows
  if (id == SET_VIDEO_MODE) strlcpy(v, ft_mode_name(focused ? mode_pick : s.ft_video_mode), sizeof(v));
  else settings_value_text(id, &s, v, sizeof(v));
  ft_draw_text(right - VALUE_W / 2, y + ROW_H / 2, FONT, s.color_text, FT81x_OPT_CENTER, v);
  if (arrows) {
    ft_draw_text(right - VALUE_W + 12, y + ROW_H / 2, FONT, s.color_text, FT81x_OPT_CENTER, "<");
    ft_draw_text(right - 12, y + ROW_H / 2, FONT, s.color_text, FT81x_OPT_CENTER, ">");
  }
}

static void draw_page() {
  const page_t *p = cur_page();
  int16_t w = panel_w();
  ft_draw_text(PANEL_X + 16, TOP + TAB_H / 2, 29, s.color_text, FT81x_OPT_CENTERY, p->name);
  ft_draw_rect(PANEL_X + 16, TOP + TAB_H + 2, w - 32, 1, 1, s.color_copyright);
  for (uint8_t i = 0; i < p->len; i++) {
    int16_t y = TOP + 40 + i * ROW_STEP;
    int16_t right = PANEL_X + w - 16;
    uint8_t kind = settings[p->ids[i]].kind;
    bool focused = focus == F_PAGE && i == row;
    // the toggle and the arrows before the row: the first area that holds the pointer wins
    bool hovered = false;
    if (kind == SET_KIND_BOOL) {
      hovered |= ft_hit_add(right - 64, y, 80, ROW_H, HIT_TOGGLE + i);
    } else if (kind == SET_KIND_CHOICE) {
      hovered |= ft_hit_add(right - VALUE_W, y, 28, ROW_H, HIT_LESS + i);
      hovered |= ft_hit_add(right - 28, y, 44, ROW_H, HIT_MORE + i);
    }
    hovered |= ft_hit_add(PANEL_X, y, w, ROW_H, HIT_ROW + i);
    if (focused || hovered) ft_draw_rect(PANEL_X, y, w, ROW_H, 4, s.color_button);
    if (focused) ft_page_marker(PANEL_X, y, ROW_H, s.color_active, ft_opt());
    const char *label = settings[p->ids[i]].label;
    if (p->ids[i] == SET_AUTOLOAD) label = "Autoload";
    ft_draw_text(PANEL_X + 16, y + ROW_H / 2, FONT, s.color_text, FT81x_OPT_CENTERY, label);
    draw_value(p->ids[i], y, focused, focused || hovered);
  }
}

static void draw_custom() {
  int16_t w = panel_w();
  ft_draw_text(PANEL_X + 16, TOP + TAB_H / 2, 29, s.color_text, FT81x_OPT_CENTERY, tab_names[tab]);
  ft_draw_rect(PANEL_X + 16, TOP + TAB_H + 2, w - 32, 1, 1, s.color_copyright);
  ft_page_ctx_t c;
  c.s = &s;
  c.x = PANEL_X;
  c.y = TOP + 40;
  c.w = w;
  c.h = ft_height() - FOOTER_H - c.y;
  c.opt = ft_opt();
  c.focused = focus == F_CUSTOM;
  if (!c.focused) ft_hit_add(c.x, c.y, c.w, c.h, HIT_PANEL); // a click opens it
  custom_page()->draw(&c);
}

// the editor of one color: R, G, B sliders
static void draw_color() {
  uint8_t id = cur_id();
  uint32_t c = *settings_color(id, &s);
  int16_t w = panel_w();
  char b[40];
  snprintf(b, sizeof(b), "Theme > %s", settings[id].label);
  ft_draw_text(PANEL_X + 16, TOP + TAB_H / 2, 29, s.color_text, FT81x_OPT_CENTERY, b);
  ft_draw_rect(PANEL_X + 16, TOP + TAB_H + 2, w - 32, 1, 1, s.color_copyright);
  ft_draw_rect(PANEL_X + 16, TOP + 48, 96, 64, 4, c);
  settings_value_text(id, &s, b, sizeof(b));
  ft_draw_text(PANEL_X + 128, TOP + 80, 30, s.color_text, FT81x_OPT_CENTERY, b);

  static const char *const names[3] = { "R", "G", "B" };
  static const uint32_t fills[3] = { 0xd02020, 0x20b020, 0x3050ff };
  for (uint8_t i = 0; i < 3; i++) {
    int16_t y = TOP + 128 + i * ROW_STEP;
    uint8_t v = (c >> (16 - 8 * i)) & 0xff;
    bool hovered = ft_hit_add(PANEL_X + 40, y, w - 48 - 56, ROW_H, HIT_SLIDER + i);
    hovered |= ft_hit_add(PANEL_X, y, w, ROW_H, HIT_CHAN + i);
    if (i == color_ch || hovered) ft_draw_rect(PANEL_X, y, w, ROW_H, 4, s.color_button);
    if (i == color_ch) ft_page_marker(PANEL_X, y, ROW_H, s.color_active, ft_opt());
    ft_draw_text(PANEL_X + 16, y + ROW_H / 2, FONT, s.color_text, FT81x_OPT_CENTERY, names[i]);
    ft_draw_slider(PANEL_X + 48, y + ROW_H / 2 - 5, w - 48 - 72, 10, fills[i], s.color_text, s.color_copyright, ft_opt(), v, 255);
    snprintf(b, sizeof(b), "%u", v);
    ft_draw_text(PANEL_X + w - 16, y + ROW_H / 2, FONT, s.color_text, FT81x_OPT_RIGHTX | FT81x_OPT_CENTERY, b);
  }
}

// an opaque box: a dim layer over the whole screen is too much for the FT812 where the system
// clock is the pixel clock (800x600), parts of the lines are left out
static void draw_dialog() {
  int16_t w = 400, h = 160;
  int16_t x = (ft_width() - w) / 2, y = (ft_height() - h) / 2;
  ft_hit_clear(); // only the buttons of the dialog take clicks
  ft_draw_rect(x - 3, y - 3, w + 6, h + 6, 8, s.color_active);
  ft_draw_rect(x, y, w, h, 8, s.color_button);
  char title[40], text[40];
  const char *left, *right;
  if (dialog == D_MODE) {
    snprintf(title, sizeof(title), "Keep %s?", ft_mode_name(mode_cur));
    uint32_t t = ms_since(mode_timer) < MODE_TEST_S * 1000 ? (MODE_TEST_S * 1000 - ms_since(mode_timer) + 999) / 1000 : 0;
    snprintf(text, sizeof(text), "Going back in %lu s", (unsigned long) t);
    left = "Keep";
    right = "Revert";
  } else {
    strlcpy(title, "Drop the changes?", sizeof(title));
    strlcpy(text, "They are not saved", sizeof(text));
    left = "Drop";
    right = "Stay";
  }
  ft_draw_text(x + w / 2, y + 30, 29, s.color_text, FT81x_OPT_CENTER, title);
  ft_draw_text(x + w / 2, y + 66, FONT_SMALL, s.color_copyright, FT81x_OPT_CENTER, text);
  if (ft_hit_add(x + 40, y + h - 56, 140, TAB_H, HIT_DLG_LEFT)) ft_draw_rect(x + 40, y + h - 56, 140, TAB_H, 2, s.color_active);
  if (ft_hit_add(x + w - 180, y + h - 56, 140, TAB_H, HIT_DLG_RIGHT)) ft_draw_rect(x + w - 180, y + h - 56, 140, TAB_H, 2, s.color_active);
  ft_draw_button(x + 40, y + h - 56, 140, TAB_H, FONT, dialog_left ? s.color_text_active : s.color_text,
    dialog_left ? s.color_active : s.color_bg, ft_opt(), left);
  ft_draw_button(x + w - 180, y + h - 56, 140, TAB_H, FONT, !dialog_left ? s.color_text_active : s.color_text,
    !dialog_left ? s.color_active : s.color_bg, ft_opt(), right);
}

static void draw_footer() {
  const char *hint = "";
  const char *keys = "";
  switch (focus) {
    case F_TABS:
      hint = custom_page() ? "Enter: open" :
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
    case F_DIALOG:
      keys = "Left/Right: choose   Enter: OK   Esc: back";
      break;
    case F_CUSTOM:
      hint = custom_page()->hint();
      keys = custom_page()->keys();
      break;
  }
  ft_draw_text(TAB_X, ft_height() - 40, FONT_SMALL, error ? ERROR_COLOR : s.color_copyright, FT81x_OPT_CENTERY, error ? error : hint);
  ft_draw_text(TAB_X, ft_height() - 20, FONT_SMALL, s.color_copyright, FT81x_OPT_CENTERY, keys);
}

void app_ft_setup_draw() {
  if (!has_ft) return;
  // the matrix shows the plugin of the Matrix effects setting with the focus
  settings_preview(focus == F_PAGE && mx_page ? cur_id() : SET_COUNT);
  ft_begin_display_list();
  ft_hit_clear();
  app_core_browser_ft_background(&s);
  bool page = tab < T_PAGES;
  // the picture only where it does not cover the settings
  bool show_char = (!page && !custom_page()) || PANEL_X + panel_w() < ft_width() - BG_WIDTH - 8;
  app_core_browser_ft_decor(&s, show_char);
  draw_tabs();
  if (focus == F_COLOR) draw_color();
  else if (page) draw_page();
  else if (custom_page()) draw_custom();
  draw_footer();
  if (focus == F_DIALOG) draw_dialog();
  hover = ft_hover();
  ft_frame_end();
}

void app_ft_setup_tz_saved(int16_t tz) {
  s.tz = tz;
}

static void set_mode(uint8_t m) {
  if (ft_init(m)) {
    mode_cur = m;
  } else {
    d_printf("FT81x did not come up in video mode %u", m); d_println();
    ft_init(mode_cur);
  }
  app_core_browser_ft_load();
}

void app_ft_setup_overlay() {
  ft_mouse_hide();
  dragging = false;
  s = hw_setup;
  settings_begin(&s);
  focus = F_TABS;
  tab = T_SYSTEM;
  shown = NULL;
  row = 0;
  error = NULL;
  mode_cur = hw_setup.ft_video_mode;
  mode_pick = s.ft_video_mode;
  hid_kbd_report_sync();
  prev_kbd = usb_keyboard_report; // the key that opened the setup is not a press here
  app_ft_setup_draw();
}

// show / hide of the custom pages as their tabs get chosen
static void track_tab() {
  const ft_page_t *p = osd_state == state_ft_setup ? custom_page() : NULL;
  if (p == shown) return;
  if (shown && shown->hide) shown->hide();
  shown = p;
  if (shown && shown->show) shown->show();
}

// the keys go back from a custom page to the tabs
static void to_tabs() {
  if (focus == F_CUSTOM) custom_page()->close();
  focus = F_TABS;
  mx_page = false;
}

static void custom_key(uint8_t key, bool shift, bool joy) {
  if (!custom_page()->key(key, shift, joy)) to_tabs();
}

static void leave() {
  to_tabs();
  if (shown && shown->hide) shown->hide();
  shown = NULL;
  if (mode_cur != hw_setup.ft_video_mode) set_mode(hw_setup.ft_video_mode);
  settings_end();
  osd_state = osd_return_state;
}

static void mouse_tick();

void app_ft_setup_tick() {
  if (!has_ft) return;
  if (focus == F_DIALOG && dialog == D_MODE && ms_since(mode_timer) >= MODE_TEST_S * 1000) {
    set_mode(mode_old);
    focus = F_PAGE;
    app_ft_setup_draw();
  }
  track_tab();
  const ft_page_t *p = custom_page();
  if (p && p->tick && (focus == F_CUSTOM || p->show) && p->tick()) app_ft_setup_draw();
  mouse_tick();
}

static void open_tab() {
  switch (tab) {
    case T_SAVE:
      if (save_setup(&s)) { leave(); return; }
      error = "Can't save karabas.ini";
      break;
    case T_CANCEL:
      leave();
      break;
    default:
      if (custom_page()) {
        focus = F_CUSTOM;
        custom_page()->open();
        break;
      }
      focus = F_PAGE;
      row = 0;
      mx_page = false;
      mode_pick = s.ft_video_mode;
  }
}

static void open_mx_page() {
  mx_page = true;
  row = 0;
}

// back from Matrix effects to its row on the System page
static void close_mx_page() {
  mx_page = false;
  row = page_row(SET_MX_EFFECTS);
}

static void change(int d) {
  uint8_t id = cur_id();
  if (settings[id].kind == SET_KIND_PAGE) {
    if (d > 0) open_mx_page();
    return;
  }
  if (id == SET_VIDEO_MODE) {
    uint8_t n = ft_mode_count();
    mode_pick = (mode_pick + d + n) % n;
    return;
  }
  settings_change(id, &s, d);
  if (id == SET_SOUND && s.ft_sound > 0) app_core_browser_ft_sound(s.ft_sound, false);
}

static void enter_row() {
  uint8_t id = cur_id();
  if (settings[id].kind == SET_KIND_COLOR) {
    focus = F_COLOR;
    color_ch = 0;
    color_old = *settings_color(id, &s);
  } else if (id == SET_VIDEO_MODE) {
    if (mode_pick == mode_cur) return;
    mode_old = mode_cur;
    set_mode(mode_pick);
    if (mode_cur != mode_pick) {
      error = "The video mode did not start";
      return;
    }
    focus = F_DIALOG;
    dialog = D_MODE;
    dialog_left = false;
    mode_timer = ms_now();
  } else {
    change(1);
  }
}

static void change_color(int d) {
  uint32_t *c = settings_color(cur_id(), &s);
  uint8_t shift = 16 - 8 * color_ch;
  int v = ((*c >> shift) & 0xff) + d;
  v = v < 0 ? 0 : v > 255 ? 255 : v;
  *c = (*c & ~(0xfful << shift)) | ((uint32_t) v << shift);
}

typedef struct {
  bool up, down, left, right, enter, esc, back, tab, shift;
} keys_t;

static void handle(const keys_t *k) {
  bool up = k->up, down = k->down, left = k->left, right = k->right, enter = k->enter, esc = k->esc, back = k->back, shift = k->shift;
  error = NULL;

  switch (focus) {
    case F_TABS:
      if (up) tab = (tab + T_COUNT - 1) % T_COUNT;
      else if (down || k->tab) tab = (tab + 1) % T_COUNT;
      else if (enter || (right && tab < T_SAVE)) open_tab();
      else if ((esc || (left && !ui_repeat)) && settings_differ(&s, &hw_setup)) { focus = F_DIALOG; dialog = D_DROP; dialog_left = false; }
      else if (esc || (left && !ui_repeat)) leave(); // Left: the tabs are the top level
      break;
    case F_PAGE: {
      uint8_t n = cur_page()->len;
      if (up) row = (row + n - 1) % n;
      else if (down) row = (row + 1) % n;
      else if (k->tab) { tab = (tab + 1) % T_PAGES; row = 0; mx_page = false; }
      else if (left || right) change(left ? -1 : 1);
      else if (enter) enter_row();
      else if (back && mx_page) close_mx_page();
      else if (back) focus = F_TABS;
      if (!up && !down) break;
      mode_pick = s.ft_video_mode;
      break;
    }
    case F_COLOR:
      if (up) color_ch = (color_ch + 2) % 3;
      else if (down || k->tab) color_ch = (color_ch + 1) % 3;
      else if (left || right) change_color((left ? -1 : 1) * (shift ? 1 : COLOR_STEP));
      else if (enter) focus = F_PAGE;
      else if (back) { *settings_color(cur_id(), &s) = color_old; focus = F_PAGE; }
      break;
    case F_DIALOG:
      if (left || right) dialog_left = !dialog_left;
      else if (dialog == D_MODE && enter && dialog_left) { s.ft_video_mode = mode_cur; focus = F_PAGE; }
      else if (dialog == D_MODE && (enter || esc)) { set_mode(mode_old); focus = F_PAGE; }
      else if (dialog == D_DROP && enter && dialog_left) leave();
      else if (dialog == D_DROP && (enter || esc)) focus = F_TABS;
      break;
    case F_CUSTOM: // the right mouse button
      if (esc) custom_key(KEY_ESC, false, false);
      break;
  }

  if (osd_state != state_ft_setup) return; // left the setup
  if (s.ft_click) ft_play_sound();
  app_ft_setup_draw();
}

// the keys of a custom page: every key pressed (text fields), the joystick as the arrows
static void custom_on_keyboard(const uint8_t *keys, uint8_t n) {
  bool shift = usb_keyboard_report.modifier & (KEY_MOD_LSHIFT | KEY_MOD_RSHIFT);
  for (uint8_t i = 0; i < n && focus == F_CUSTOM && osd_state == state_ft_setup; i++) {
    ft_mouse_hide();
    custom_key(keys[i], shift, false);
  }
  if (n == 0) {
    uint8_t key = ui_joy_key();
    if (!key) return;
    ft_mouse_hide();
    custom_key(key, false, true);
    n = 1;
  }
  if (n == 0 || osd_state != state_ft_setup) return;
  if (s.ft_click) ft_play_sound();
  app_ft_setup_draw();
}

void app_ft_setup_on_keyboard() {
  uint8_t keys[6];
  uint8_t n = ui_pressed_keys(&prev_kbd, keys);
  if (focus == F_CUSTOM) {
    custom_on_keyboard(keys, n);
    return;
  }
  uint8_t key = ui_nav_key();
  keys_t k;
  k.up = key == KEY_UP;
  k.down = key == KEY_DOWN;
  k.left = key == KEY_LEFT;
  k.right = key == KEY_RIGHT;
  k.enter = key == KEY_ENTER;
  k.esc = key == KEY_ESC;
  k.back = k.esc || key == KEY_BACKSPACE;
  k.tab = key == KEY_TAB;
  k.shift = usb_keyboard_report.modifier & (KEY_MOD_LSHIFT | KEY_MOD_RSHIFT);
  if (!(k.up || k.down || k.left || k.right || k.enter || k.back || k.tab)) return;
  ft_mouse_hide(); // the keys move the focus
  handle(&k);
}

// ---------------- the mouse ----------------

static void slider_set(int16_t x) {
  int16_t sx = PANEL_X + 48, sw = panel_w() - 48 - 72;
  int v = (int) (x - sx) * 255 / sw;
  v = v < 0 ? 0 : v > 255 ? 255 : v;
  uint32_t *c = settings_color(cur_id(), &s);
  uint8_t shift = 16 - 8 * color_ch;
  *c = (*c & ~(0xfful << shift)) | ((uint32_t) v << shift);
}

// pick a row of the page; the video mode picked on another row is dropped
static void pick_row(uint8_t i) {
  if (focus != F_PAGE || row != i) mode_pick = s.ft_video_mode;
  focus = F_PAGE;
  row = i;
}

// true if it went through handle(), which draws
static bool click(int16_t id) {
  if (id >= HIT_TAB && id < HIT_TAB + T_COUNT) {
    to_tabs();
    tab = id - HIT_TAB;
    if (tab >= T_SAVE) open_tab();
  } else if (id == HIT_PANEL) {
    open_tab();
  } else if (id >= FT_PAGE_HIT && focus == F_CUSTOM) {
    if (!custom_page()->click(id - FT_PAGE_HIT)) to_tabs();
  } else if (id >= HIT_ROW && id < HIT_ROW + 16) {
    uint8_t i = id - HIT_ROW;
    if (focus == F_PAGE && row == i) enter_row();
    else pick_row(i);
  } else if (id >= HIT_TOGGLE && id < HIT_TOGGLE + 16) {
    pick_row(id - HIT_TOGGLE);
    change(1);
  } else if (id >= HIT_LESS && id < HIT_LESS + 16) {
    pick_row(id - HIT_LESS);
    change(-1);
  } else if (id >= HIT_MORE && id < HIT_MORE + 16) {
    pick_row(id - HIT_MORE);
    change(1);
  } else if (id >= HIT_CHAN && id < HIT_CHAN + 3) {
    color_ch = id - HIT_CHAN;
  } else if (id >= HIT_SLIDER && id < HIT_SLIDER + 3) {
    color_ch = id - HIT_SLIDER;
    dragging = true;
    slider_set(ft_mouse_x());
  } else if (id == HIT_DLG_LEFT || id == HIT_DLG_RIGHT) {
    dialog_left = id == HIT_DLG_LEFT;
    keys_t k = {};
    k.enter = true;
    handle(&k);
    return true;
  }
  return false;
}

static void wheel(int16_t id, int d) {
  if (id >= HIT_TAB && id < HIT_TAB + T_COUNT) {
    to_tabs();
    tab = (tab - d + T_COUNT) % T_COUNT; // up: the section above
  } else if (focus == F_CUSTOM) {
    if (custom_page()->wheel) custom_page()->wheel(id >= FT_PAGE_HIT ? id - FT_PAGE_HIT : FT_HIT_NONE, d);
  } else if (id >= HIT_ROW && id < HIT_MORE + 16) {
    pick_row((id - HIT_ROW) % 100);
    change(d);
  } else if (id >= HIT_CHAN && id < HIT_SLIDER + 3) {
    color_ch = (id - HIT_CHAN) % 100;
    change_color(d * COLOR_STEP);
  }
}

static void mouse_tick() {
  ft_mouse_event_t ev;
  if (!ft_mouse_poll(&ev)) return;
  bool redraw = false;
  if (dragging) {
    if ((ev.buttons & FT_MOUSE_LEFT) && focus == F_COLOR) {
      if (ev.moved) { slider_set(ft_mouse_x()); redraw = true; }
    } else {
      dragging = false;
    }
  }
  if (ev.pressed & FT_MOUSE_RIGHT) {
    keys_t k = {};
    k.esc = k.back = true;
    handle(&k); // draws
    return;
  }
  if (ev.pressed || ev.wheel) {
    error = NULL;
    if ((ev.pressed & FT_MOUSE_LEFT) && click(ft_hover())) return;
    if (ev.wheel) wheel(ft_hover(), ev.wheel);
    if (osd_state != state_ft_setup) return; // left the setup
    if (s.ft_click) ft_play_sound();
    redraw = true;
  }
  if (redraw || ft_hover() != hover) app_ft_setup_draw();
  else if (ev.moved) ft_pointer_frame();
}
