#include "main.h"
#include "app_core.h"
#include "app_core_browser.h"
#include "app_file_loader.h"
#include "app_setup.h"
#include "app_about.h"
#include "app_pad_mapper.h"
#include "app_rtc.h"
#include "app_wifi.h"

// OSD state and the app screens (one per osd_state)

OSD zxosd;
volatile bool is_osd = false;       // also read by the input task
volatile bool is_osd_hiding = false;
bool need_redraw = false;
bool has_ft = false;
ElapsedTimer hide_timer;
uint8_t osd_state;
uint8_t osd_return_state;

static bool is_popup_hiding = false;
static ElapsedTimer popup_timer;
static uint8_t osd_prev_state = state_main;

typedef struct {
  void (*overlay)();
  void (*on_keyboard)();
  void (*tick)(); // optional, every loop while the app owns the OSD
} app_t;

static void app_core_browser_show() {
  if (hw_setup.ft_enabled && has_ft == true) {
    app_core_browser_ft_overlay();
  } else {
    app_core_browser_overlay();
  }
}

// indexed by osd_state_e
static const app_t apps[] = {
  { []() { app_core_overlay(true); },                 app_core_on_keyboard },         // state_main
  { app_setup_overlay,                                 app_setup_on_keyboard },        // state_setup
  { app_about_overlay,                                 app_about_on_keyboard },        // state_about
  { app_core_browser_show,                             app_core_browser_on_keyboard }, // state_core_browser
  { []() { app_file_loader_overlay(true, false); },   app_file_loader_on_keyboard },  // state_file_loader
  { app_pad_mapper_overlay,                            app_pad_mapper_on_keyboard,     app_pad_mapper_tick }, // state_pad_mapper
  { app_rtc_overlay,                                   app_rtc_on_keyboard },          // state_rtc
  { app_wifi_overlay,                                  app_wifi_on_keyboard,           app_wifi_tick }, // state_wifi
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
  popup_timer.reset();
  zxosd.showPopup(i);
}

void ui_tick() {
  if (is_osd && osd_state < apps_len && apps[osd_state].tick) {
    apps[osd_state].tick();
  }

  // set is_osd off after 200ms of real switching off 
  // to avoid esc keypress passes to the host
  if (is_osd_hiding && hide_timer.elapsed() >= 200) {
    is_osd = false;
    is_osd_hiding = false;
  }

  // hide popup after 500 ms
  if (is_popup_hiding && popup_timer.elapsed() >= 500) {
    is_popup_hiding = false;
    zxosd.hidePopup();
    osd_handle(true); // reinit osd
  }
}

void ui_button(uint8_t x, uint8_t y, uint8_t width, const char *label, bool focused) {
  zxosd.setColor(OSD::COLOR_BLACK, focused ? OSD::COLOR_YELLOW_I : OSD::COLOR_GREY);
  zxosd.fill(x, y, x + width - 1, y, ' ');
  uint8_t len = strlen(label);
  zxosd.setPos(x + (len < width ? (width - len) / 2 : 0), y);
  zxosd.print(label);
}

void print_time() {
  zxosd.setPos(24, 0);
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  static bool dots_blink = !dots_blink;
  String dots = String(dots_blink ? ':' : ' ');
  uint8_t h = zxrtc.getHour();
  uint8_t m = zxrtc.getMinute();
  uint8_t s = zxrtc.getSecond();
  if (h < 10) zxosd.print(0); zxosd.print(h);
  if (dots_blink) zxosd.print(":"); else zxosd.print(" ");
  if (m < 10) zxosd.print(0); zxosd.print(m);
  if (dots_blink) zxosd.print(":"); else zxosd.print(" ");
  if (s < 10) zxosd.print(0); zxosd.print(s);

  zxosd.setPos(24, 1);
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  uint8_t d = zxrtc.getDay();
  uint8_t mo = zxrtc.getMonth();
  uint8_t y = zxrtc.getYear();
  if (d < 10) zxosd.print(0); zxosd.print(d); zxosd.print("/");
  if (mo < 10) zxosd.print(0); zxosd.print(mo); zxosd.print("/");
  if (y < 10) zxosd.print(0); zxosd.print(y);
}

void on_time() {
  print_time();
  zxosd.update();

  // on time callback
  app_core_browser_on_time();

  if (core.type == CORE_TYPE_BOOT && has_ft == true && is_osd == true) {
    // redraw core browser
    app_core_browser_ft_menu(0);
  }
}
