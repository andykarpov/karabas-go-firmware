#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "OSD.h"
#include "usb_hid_keys.h"
#include "SegaController.h"
#include "file_name.h"
#include "app_core_browser.h"
#include "app_settings.h"

// Setup > Settings (BOOT core only): the karabas.ini settings. Up / Down / Tab pick a setting,
// Left / Right / Enter change it. Save writes the changed values into karabas.ini and keeps the
// rest of the file. The look of the graphic menu is set in the graphic menu itself.

enum {
  I_DEBUG, I_DEBUG_HID, I_BURST, I_DVI,
  I_AUTOLOAD, I_TIMEOUT, I_CORE,
  I_FT, I_VIDEO_MODE,
  I_SAVE, I_CANCEL, I_COUNT
};

typedef struct {
  uint8_t row;
  const char *label;
  const char *hint;
} item_t;

// in the order of the enum, the buttons excluded
static const item_t items[] = {
  {  9, "Debug log",     "Log to the USB serial port" },
  { 10, "HID debug log", "Log the USB HID reports" },
  { 11, "Burst link",    "Fast transfers, next core" },
  { 12, "DVI only",      "No HDMI audio, next core" },
  { 14, "Enabled",       "Start a core on power on" },
  { 15, "Timeout",       "Seconds before the start" },
  { 16, "Core",          "The core to start" },
  { 18, "Enabled",       "Graphic core browser" },
  { 19, "Video mode",    "Space at power on: text menu" },
};

#define ROW_SYSTEM 8
#define ROW_AUTOLOAD 13
#define ROW_FT 17
#define ROW_BUTTONS 21
#define ROW_HINT 22
#define LABEL_X 2
#define VALUE_X 16 // '<', the value, '>'
#define VALUE_WIDTH 13
#define BTN_WIDTH 10
#define TIMEOUT_MAX 60

static setup_t s;     // the edited copy of hw_setup
static uint8_t focus;
static uint8_t core_sel;  // in cores[], cores_len: the core of karabas.ini that is not on the card
static bool core_other;   // there is such a core
static const char *error;

// the header fields of the cores are padded with spaces
static void trim_copy(char *out, const char *in, size_t size) {
  while (*in == ' ') in++;
  strlcpy(out, in, size);
  size_t len = strlen(out);
  while (len > 0 && out[len - 1] == ' ') out[--len] = '\0';
}

static uint8_t core_options() {
  return cores_len + (core_other ? 1 : 0);
}

static void value_text(uint8_t i, char *out, size_t size) {
  switch (i) {
    case I_DEBUG: strlcpy(out, s.debug_enabled ? "On" : "Off", size); break;
    case I_DEBUG_HID: strlcpy(out, s.debug_hid ? "On" : "Off", size); break;
    case I_BURST: strlcpy(out, s.burst_enabled ? "On" : "Off", size); break;
    case I_DVI: strlcpy(out, s.dvi_only ? "On" : "Off", size); break;
    case I_AUTOLOAD: strlcpy(out, s.autoload_enabled ? "On" : "Off", size); break;
    case I_TIMEOUT: snprintf(out, size, "%u s", s.autoload_timeout); break;
    case I_CORE: {
      char name[sizeof(cores[0].name)];
      trim_copy(name, core_sel < cores_len ? cores[core_sel].name : s.autoload_core, sizeof(name));
      kg_short_name(name[0] ? name : "-", out, size - 1);
      break;
    }
    case I_FT: strlcpy(out, s.ft_enabled ? "On" : "Off", size); break;
    case I_VIDEO_MODE: strlcpy(out, FT812::modeName(s.ft_video_mode), size); break;
    default: out[0] = '\0';
  }
}

static void draw_item(uint8_t i) {
  const item_t *it = &items[i];
  bool focused = focus == i;
  zxosd.setColor(focused ? OSD::COLOR_YELLOW_I : OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.setPos(LABEL_X, it->row); zxosd.print(it->label);

  char value[VALUE_WIDTH + 1];
  value_text(i, value, sizeof(value));
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.fill(VALUE_X, it->row, VALUE_X + VALUE_WIDTH + 1, it->row, ' ');
  if (focused) {
    zxosd.setPos(VALUE_X, it->row); zxosd.print("<");
    zxosd.setPos(VALUE_X + VALUE_WIDTH + 1, it->row); zxosd.print(">");
    zxosd.setColor(OSD::COLOR_BLACK, OSD::COLOR_YELLOW_I);
  } else {
    zxosd.setColor(OSD::COLOR_CYAN_I, OSD::COLOR_BLACK);
  }
  zxosd.setPos(VALUE_X + 1, it->row); zxosd.print(value);
}

static void draw() {
  for (uint8_t i = 0; i < I_SAVE; i++) draw_item(i);
  ui_button(1, ROW_BUTTONS, BTN_WIDTH, "Save", focus == I_SAVE);
  ui_button(12, ROW_BUTTONS, BTN_WIDTH, "Cancel", focus == I_CANCEL);

  zxosd.setColor(error ? OSD::COLOR_RED_I : OSD::COLOR_GREY, OSD::COLOR_BLACK);
  zxosd.fill(0, ROW_HINT, 31, ROW_HINT, ' ');
  zxosd.setPos(1, ROW_HINT);
  if (error) zxosd.print(error);
  else if (focus < I_SAVE) zxosd.print(items[focus].hint);
  zxosd.update();
}

static void section(uint8_t row, const char *title) {
  zxosd.setColor(OSD::COLOR_GREEN_I, OSD::COLOR_BLACK);
  zxosd.setPos(1, row); zxosd.print(title);
}

void app_settings_overlay() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();
  zxosd.header(core.build, core.id, HW_ID);
  print_time();
  zxosd.setPos(0, 5);
  zxosd.print("Settings");
  zxosd.line(6);

  s = hw_setup;
  focus = I_DEBUG;
  error = nullptr;
  if (s.autoload_timeout > TIMEOUT_MAX) s.autoload_timeout = TIMEOUT_MAX;
  trim_copy(s.autoload_core, hw_setup.autoload_core, sizeof(s.autoload_core));
  core_sel = cores_len;
  for (uint8_t i = 0; i < cores_len; i++) {
    char id[sizeof(cores[0].id)];
    trim_copy(id, cores[i].id, sizeof(id));
    if (strcmp(id, s.autoload_core) == 0) { core_sel = i; break; }
  }
  core_other = core_sel == cores_len;

  section(ROW_SYSTEM, "System");
  section(ROW_AUTOLOAD, "Autoload");
  section(ROW_FT, "Graphic menu (FT812)");

  // footer
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.line(23);
  zxosd.setPos(1, 24); zxosd.print("Up/Down: select, Left/Right:");
  zxosd.setPos(1, 25); zxosd.print("change, Esc: cancel");
  draw();
}

// d: +1 / -1
static void change(int d) {
  switch (focus) {
    case I_DEBUG: s.debug_enabled = !s.debug_enabled; break;
    case I_DEBUG_HID: s.debug_hid = !s.debug_hid; break;
    case I_BURST: s.burst_enabled = !s.burst_enabled; break;
    case I_DVI: s.dvi_only = !s.dvi_only; break;
    case I_AUTOLOAD: s.autoload_enabled = !s.autoload_enabled; break;
    case I_TIMEOUT: s.autoload_timeout = (s.autoload_timeout + d + TIMEOUT_MAX + 1) % (TIMEOUT_MAX + 1); break;
    case I_CORE: {
      uint8_t n = core_options();
      if (n == 0) break;
      core_sel = (core_sel + d + n) % n;
      if (core_sel < cores_len) trim_copy(s.autoload_core, cores[core_sel].id, sizeof(s.autoload_core));
      else trim_copy(s.autoload_core, hw_setup.autoload_core, sizeof(s.autoload_core));
      break;
    }
    case I_FT: s.ft_enabled = !s.ft_enabled; break;
    case I_VIDEO_MODE: {
      uint8_t n = FT812::modeCount();
      s.ft_video_mode = (s.ft_video_mode + d + n) % n;
      break;
    }
  }
}

void app_settings_on_keyboard() {
  uint8_t key = usb_keyboard_report.keycode[0];
  bool up = key == KEY_UP || (joyL & SC_BTN_UP) || (joyR & SC_BTN_UP);
  bool down = key == KEY_DOWN || key == KEY_TAB || (joyL & SC_BTN_DOWN) || (joyR & SC_BTN_DOWN);
  bool left = key == KEY_LEFT || (joyL & SC_BTN_LEFT) || (joyR & SC_BTN_LEFT);
  bool right = key == KEY_RIGHT || (joyL & SC_BTN_RIGHT) || (joyR & SC_BTN_RIGHT);
  bool enter = key == KEY_ENTER || key == KEY_KPENTER || (joyL & SC_BTN_A) || (joyR & SC_BTN_A) || (joyL & SC_BTN_B) || (joyR & SC_BTN_B);
  bool esc = key == KEY_ESC || (joyL & SC_BTN_C) || (joyR & SC_BTN_C);
  bool button = focus == I_SAVE || focus == I_CANCEL;

  if (esc) {
    osd_state = state_setup;
    return;
  }
  if (up) {
    focus = (focus + I_COUNT - 1) % I_COUNT;
  } else if (down) {
    focus = (focus + 1) % I_COUNT;
  } else if (button && (left || right)) {
    focus = focus == I_SAVE ? I_CANCEL : I_SAVE;
  } else if (!button && (left || right || enter)) {
    change(left ? -1 : 1);
  } else if (enter && focus == I_SAVE) {
    if (save_setup(s)) { osd_state = state_setup; return; }
    error = "Can't save karabas.ini";
    draw();
    return;
  } else if (enter && focus == I_CANCEL) {
    osd_state = state_setup;
    return;
  } else {
    return;
  }
  error = nullptr;
  draw();
}
