#include "main.h"
#include "ps2kbd.h"
#include "usb_hid_keys.h"
#include "app_core.h"
#include "app_core_browser.h"
#include "app_file_loader.h"
#include "app_setup.h"
#include "app_about.h"
#include "app_pad_mapper.h"
#include "app_rtc.h"
#include "app_wifi.h"
#include "app_settings.h"
#include "app_ft_setup.h"
#include "app_ft_pages.h"
#include "app_msc.h"
#include "masked_input.h"

// OSD state and the app screens (one per osd_state)

volatile bool is_osd = false;       // also read by the input task
volatile bool is_osd_hiding = false;
bool need_redraw = false;
bool has_ft = false;
uint32_t hide_timer = 0;
uint8_t osd_state;
uint8_t osd_return_state;
bool ui_repeat = false;

static bool is_popup_hiding = false;
static uint32_t popup_timer = 0;
static uint8_t osd_prev_state = state_main;

typedef struct {
  void (*overlay)();
  void (*on_keyboard)();
  void (*tick)(); // optional, every loop while the app owns the OSD
} app_t;

static void app_core_browser_show() {
  if (has_ft) {
    app_core_browser_ft_overlay();
  } else {
    app_core_browser_overlay();
  }
}

static void app_core_show() { app_core_overlay(true); }
static void app_file_loader_show() { app_file_loader_overlay(true, false); }

// indexed by osd_state_e
static const app_t apps[] = {
  { app_core_show,                                     app_core_on_keyboard },         // state_main
  { app_setup_overlay,                                 app_setup_on_keyboard },        // state_setup
  { app_about_overlay,                                 app_about_on_keyboard },        // state_about
  { app_core_browser_show,                             app_core_browser_on_keyboard,   app_core_browser_tick }, // state_core_browser
  { app_file_loader_show,                              app_file_loader_on_keyboard },  // state_file_loader
  { app_pad_mapper_overlay,                            app_pad_mapper_on_keyboard,     app_pad_mapper_tick }, // state_pad_mapper
  { app_rtc_overlay,                                   app_rtc_on_keyboard,            app_rtc_tick }, // state_rtc
  { app_wifi_overlay,                                  app_wifi_on_keyboard,           app_wifi_tick }, // state_wifi
  { app_settings_overlay,                              app_settings_on_keyboard },     // state_settings
  { app_ft_setup_overlay,                              app_ft_setup_on_keyboard,       app_ft_setup_tick }, // state_ft_setup
  { app_msc_overlay,                                   app_msc_on_keyboard },          // state_msc
};
static const uint8_t apps_len = sizeof(apps) / sizeof(apps[0]);

void osd_handle(bool force) {
  if (is_osd || force) {
    if ((osd_prev_state != osd_state) || force) {
      osd_prev_state = osd_state;
      if (osd_state < apps_len) {
        apps[osd_state].overlay();
      }
    }
  }
}

void ui_on_keyboard() {
  if (is_osd && osd_state < apps_len) {
    apps[osd_state].on_keyboard();
  }
}

// show a popup and hide it after 500 ms
void ui_popup_start(uint8_t i) {
  is_popup_hiding = true;
  popup_timer = ms_now();
  osd_show_popup(i);
}

void ui_tick() {
  if (is_osd && osd_state < apps_len && apps[osd_state].tick) {
    apps[osd_state].tick();
  }

  // set is_osd off after 200ms of real switching off 
  // to avoid esc keypress passes to the host
  if (is_osd_hiding && ms_since(hide_timer) >= 200) {
    is_osd = false;
    is_osd_hiding = false;
  }

  // hide popup after 500 ms
  if (is_popup_hiding && ms_since(popup_timer) >= 500) {
    is_popup_hiding = false;
    osd_hide_popup();
    osd_handle(true); // reinit osd
  }
}

void ui_button(uint8_t x, uint8_t y, uint8_t width, const char *label, bool focused) {
  osd_set_color(OSD_COLOR_BLACK, focused ? OSD_COLOR_YELLOW_I : OSD_COLOR_GREY);
  osd_fill(x, y, x + width - 1, y, ' ');
  uint8_t len = strlen(label);
  osd_set_pos(x + (len < width ? (width - len) / 2 : 0), y);
  osd_print(label);
}

void print_time() {
  osd_set_pos(24, 0);
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  // the C++ of old: a static initialized once to !false, so the dots never blinked
  static const bool dots_blink = true;
  uint8_t h = rtc_get_hour();
  uint8_t m = rtc_get_minute();
  uint8_t s = rtc_get_second();
  char sep = dots_blink ? ':' : ' ';
  osd_printf("%02u%c%02u%c%02u", h, sep, m, sep, s);

  osd_set_pos(24, 1);
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  uint8_t d = rtc_get_day();
  uint8_t mo = rtc_get_month();
  uint8_t y = rtc_get_year();
  osd_printf("%02u/%02u/%02u", d, mo, y);
}

void on_time() {
  print_time();
  osd_update();

  if (osd_state == state_msc) {
    if (has_ft) app_msc_ft_draw();
    return;
  }

  // on time callback
  app_core_browser_on_time();

  if (core.type == CORE_TYPE_BOOT && has_ft == true && is_osd == true) {
    // redraw the graphic menu (the clock)
    if (osd_state == state_ft_setup) {
      app_ft_setup_draw();
    } else {
      app_core_browser_ft_menu(0);
    }
  }
}

static bool in_report(const hid_keyboard_report_t *r, uint8_t key) {
  for (uint8_t i = 0; i < 6; i++) if (r->keycode[i] == key) return true;
  return false;
}

uint8_t ui_pressed_keys(hid_keyboard_report_t *prev, uint8_t *keys) {
  const hid_keyboard_report_t *r = &usb_keyboard_report;
  uint8_t n = 0;
  for (uint8_t i = 0; i < 6; i++) {
    if (r->keycode[i] && !in_report(prev, r->keycode[i])) keys[n++] = r->keycode[i];
  }
  if (n == 0 && memcmp(r, prev, sizeof(*r)) == 0 && kb_repeat_key && in_report(r, kb_repeat_key)) {
    keys[n++] = kb_repeat_key;
  }
  *prev = (*r);
  return n;
}

uint8_t ui_joy_key() {
  uint16_t joy = joyL | joyR;
  return (joy & SC_BTN_UP) ? KEY_UP : (joy & SC_BTN_DOWN) ? KEY_DOWN :
         (joy & SC_BTN_LEFT) ? KEY_LEFT : (joy & SC_BTN_RIGHT) ? KEY_RIGHT :
         (joy & (SC_BTN_A | SC_BTN_B)) ? KEY_ENTER : (joy & UI_JOY_ESC) ? KEY_ESC : 0;
}

bool ui_joy_edit(char *text, size_t max, uint8_t key) {
  size_t len = strlen(text);
  if (key == KEY_UP || key == KEY_DOWN) kg_text_turn(text, max, key == KEY_UP ? 1 : -1);
  else if (key == KEY_RIGHT) kg_text_add(text, max);
  else if (key == KEY_LEFT) { if (len > 0) text[len - 1] = '\0'; }
  else return false;
  return true;
}

bool ui_joy_repeats() {
  if (osd_state == state_pad_mapper) return app_pad_mapper_listing();
  if (osd_state == state_ft_setup) return !ft_page_pad_mapping();
  return true;
}

bool ui_joy_hotkeys() {
  return !is_osd || ui_joy_repeats();
}
