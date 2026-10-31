#include "config.h"
#include "types.h"
#include "main.h"
#include "osd.h"
#include "input/usb_hid_keys.h"
#include "input/sega.h"
#include "input/ps2kbd.h"
#include "periph/esp_at.h"
#include "apps/app_wifi.h"
#include "apps/wifi_setup.h"
#include "ui/ui_draw.h"

// Setup > Setup Wi-Fi (BOOT core only, it bridges the ESP8266 UART): apps/wifi_setup (shared with
// the Wi-Fi page of the graphic setup) asks the module, scans and joins; this screen draws it.

#define LIST_ROW 8
#define LIST_ROWS 14
// the labels of the buttons have even lengths (Retry has a screen of its own), so they center
// exactly in an even width
#define BTN_WIDTH 10
#define BTN2_X (BTN_WIDTH + 1)
#define MENU_WIDTH 18
#define VALUE_X 9
#define ROW_RESCAN 22

static hid_keyboard_report_t prev_kbd;

static void draw();

void app_wifi_tick() {
  if (ws_tick()) draw();
}

// ---------------- drawing ----------------

// s with the chars the OSD font has no glyph for as '?', at most width chars
static void print_text(const char *s, uint8_t width) {
  for (uint8_t i = 0; s[i] && i < width; i++) {
    char c = s[i];
    osd_putc((c >= 32 && c < 127) ? c : '?');
  }
}

static void draw_info(uint8_t y, const char *label, const char *value, uint8_t color) {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_set_pos(0, y); osd_print(label);
  osd_set_color(color, OSD_COLOR_BLACK);
  osd_set_pos(VALUE_X, y); print_text(value, OSD_SIZE_X - VALUE_X);
}

static void draw_message(uint8_t y, const char *msg, uint8_t color) {
  osd_set_color(color, OSD_COLOR_BLACK);
  osd_set_pos(0, y); print_text(msg, OSD_SIZE_X);
}


// the text field: the tail that fits, a cyan cursor at the end
static void draw_field(uint8_t y, const char *text, bool hidden) {
  const uint8_t width = OSD_SIZE_X;
  size_t len = strlen(text);
  size_t from = len >= width ? len - width + 1 : 0;
  osd_set_color(OSD_COLOR_BLACK, OSD_COLOR_WHITE);
  osd_set_pos(0, y);
  for (size_t i = from; i < from + width; i++) {
    if (i == len) {
      osd_set_color(OSD_COLOR_BLACK, OSD_COLOR_CYAN_I);
      osd_putc(' ');
      osd_set_color(OSD_COLOR_BLACK, OSD_COLOR_WHITE);
    } else if (i < len) {
      char c = hidden ? '*' : text[i];
      osd_putc((c >= 32 && c < 127) ? c : '?');
    } else {
      osd_putc(' ');
    }
  }
}

static void draw_list() {
  const ws_t *w = ws_state();
  ui_button(0, ROW_RESCAN, BTN_WIDTH, "Rescan", w->on_rescan || w->aps_len == 0);
  if (w->aps_len == 0) {
    draw_message(LIST_ROW, "No networks found", OSD_COLOR_YELLOW_I);
    return;
  }
  uint8_t top = ws_list_top();
  uint8_t x_end = ui_list_col_end(LIST_ROWS, w->aps_len);
  for (uint8_t row = 0; row < LIST_ROWS && top + row < w->aps_len; row++) {
    const kg_wifi_ap_t *ap = &w->aps[top + row];
    bool focused = top + row == w->sel && !w->on_rescan;
    osd_set_color(focused ? OSD_COLOR_BLACK : OSD_COLOR_WHITE, focused ? OSD_COLOR_YELLOW_I : OSD_COLOR_BLACK);
    osd_fill(0, LIST_ROW + row, x_end, LIST_ROW + row, ' ');
    osd_set_pos(0, LIST_ROW + row);
    osd_putc(strcmp(ap->ssid, w->cur_ssid) == 0 ? '>' : ' ');
    osd_printf("%4d %c ", ap->rssi, ap->ecn ? '*' : ' ');
    print_text(ap->ssid, x_end - 8 + 1); // 24 chars, 23 with the scrollbar
  }
  ui_scrollbar(LIST_ROW, LIST_ROWS, top, LIST_ROWS, w->aps_len);
  if (w->aps_len > LIST_ROWS) {
    osd_set_color(OSD_COLOR_GREY, OSD_COLOR_BLACK);
    osd_set_pos(UI_COL_LAST - 4, 7); osd_printf("%2u/%-2u", w->sel + 1, w->aps_len);
  }
}

static void draw() {
  const ws_t *w = ws_state();
  ui_clear_body();
  char buf[40];

  switch (w->screen) {
    case WS_INIT:
      draw_message(8, "Talking to ESP8266...", OSD_COLOR_YELLOW_I);
      ui_hint("Please wait", "Esc: back");
      break;
    case WS_NO_ESP:
      draw_message(8, "ESP8266 is not responding", OSD_COLOR_RED_I);
      ui_button(0, 11, BTN_WIDTH + 1, "Retry", true);
      ui_hint("Enter: retry", "Esc: back");
      break;
    case WS_STATUS:
      draw_info(8, "Module", w->version[0] ? w->version : "?", OSD_COLOR_CYAN_I);
      draw_info(9, "CTS", w->cts == WS_CTS_ON ? "on" : w->cts == WS_CTS_OFF ? "could not turn on" : "unknown",
                w->cts == WS_CTS_ON ? OSD_COLOR_CYAN_I : OSD_COLOR_RED_I);
      draw_info(10, "Network", w->cur_ssid[0] ? w->cur_ssid : "not connected", w->cur_ssid[0] ? OSD_COLOR_CYAN_I : OSD_COLOR_GREY);
      draw_info(11, "IP", w->ip[0] && strcmp(w->ip, "0.0.0.0") != 0 ? w->ip : "-", OSD_COLOR_CYAN_I);
      {
        uint8_t ids[WS_M_COUNT], n = ws_menu_items(ids);
        for (uint8_t i = 0; i < n; i++) ui_button(0, 14 + 2 * i, MENU_WIDTH, ws_menu_names[ids[i]], w->sel == i);
      }
      ui_hint("Enter: select", "Esc: back");
      break;
    case WS_SCAN:
      draw_message(8, "Scanning...", OSD_COLOR_YELLOW_I);
      ui_hint("Please wait", "Esc: back");
      break;
    case WS_LIST:
      osd_set_color(OSD_COLOR_GREY, OSD_COLOR_BLACK);
      osd_set_pos(1, 7); osd_print("dBm   Network");
      draw_list();
      ui_hint("Enter: connect, R: rescan", "* password, > joined; Esc: back");
      break;
    case WS_SSID:
      draw_message(8, "Network name:", OSD_COLOR_WHITE);
      draw_field(9, w->ssid, false);
      ui_hint("Enter: next", "Esc: back");
      break;
    case WS_PASS:
      osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
      osd_set_pos(0, 8); osd_print("Network:");
      osd_set_color(OSD_COLOR_CYAN_I, OSD_COLOR_BLACK);
      osd_set_pos(VALUE_X, 8); print_text(w->ssid, OSD_SIZE_X - VALUE_X);
      draw_message(10, "Password:", OSD_COLOR_WHITE);
      draw_field(11, w->pass, !w->show_pass);
      ui_hint("Enter: connect, Esc: back", w->show_pass ? "Tab: hide the password" : "Tab: show the password");
      break;
    case WS_JOIN:
      snprintf(buf, sizeof(buf), "Connecting to %s...", w->ssid);
      draw_message(8, buf, OSD_COLOR_YELLOW_I);
      ui_hint("Please wait", "");
      break;
    case WS_RESULT:
      if (w->result_error) {
        draw_message(8, w->result_error, OSD_COLOR_RED_I);
      } else {
        draw_message(8, "Connected", OSD_COLOR_GREEN_I);
        draw_info(10, "Network", w->cur_ssid, OSD_COLOR_CYAN_I);
        draw_info(11, "IP", w->ip[0] ? w->ip : "-", OSD_COLOR_CYAN_I);
        draw_message(13, "The ESP8266 remembers it", OSD_COLOR_GREY);
      }
      ui_button(0, 16, BTN_WIDTH, "OK", true);
      ui_hint("Enter: back", "");
      break;
    case WS_FORGET:
      draw_message(8, "Forget this network?", OSD_COLOR_YELLOW_I);
      draw_info(10, "Network", w->cur_ssid, OSD_COLOR_CYAN_I);
      draw_message(12, "The ESP8266 will not join it", OSD_COLOR_GREY);
      ui_button(0, 15, BTN_WIDTH, "Forget", w->sel == 0);
      ui_button(BTN2_X, 15, BTN_WIDTH, "Cancel", w->sel == 1);
      ui_hint("Left/Right: select", "Enter: OK, Esc: back");
      break;
    case WS_UPD_ASK:
      if (w->restoring) {
        draw_message(8, "Erase the ESP8266 settings?", OSD_COLOR_YELLOW_I);
        draw_message(10, "For modules that do not keep", OSD_COLOR_GREY);
        draw_message(11, "their settings. It forgets", OSD_COLOR_GREY);
        draw_message(12, "the network: join it again.", OSD_COLOR_GREY);
      } else {
        draw_message(8, "Update the ESP8266 firmware?", OSD_COLOR_YELLOW_I);
        draw_info(10, "Module", w->version[0] ? w->version : "?", OSD_COLOR_CYAN_I);
        draw_message(12, "From the Espressif server,", OSD_COLOR_GREY);
        draw_message(13, "about half a minute.", OSD_COLOR_GREY);
        draw_message(14, "Do not power off the board.", OSD_COLOR_GREY);
      }
      ui_button(0, 17, BTN_WIDTH, w->restoring ? "Wipe" : "Update", w->sel == 0);
      ui_button(BTN2_X, 17, BTN_WIDTH, "Cancel", w->sel == 1);
      ui_hint("Left/Right: select", "Enter: OK, Esc: back");
      break;
    case WS_UPD: {
      uint8_t percent = esp_update_progress();
      const char *msg = esp_update_message();
      draw_message(8, msg ? msg : "", OSD_COLOR_YELLOW_I);
      osd_progress(0, 10, 27, percent, 100, OSD_COLOR_CYAN_I, OSD_COLOR_BLACK);
      osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
      osd_set_pos(28, 10); osd_printf("%3u%%", percent);
      draw_message(12, "Do not power off the board", OSD_COLOR_GREY);
      ui_hint("Please wait", "");
      break;
    }
    case WS_UPD_END: {
      bool ok = esp_update_poll() == ESP_UPD_OK;
      draw_message(8, esp_update_message(), ok ? OSD_COLOR_GREEN_I : OSD_COLOR_RED_I);
      if (!w->restoring) {
        draw_info(10, ok ? "Was" : "Module", w->old_version[0] ? w->old_version : "?", OSD_COLOR_CYAN_I);
        if (ok) draw_info(11, "Now", esp_update_version()[0] ? esp_update_version() : "?", OSD_COLOR_CYAN_I);
      } else if (ok) {
        draw_message(10, "Join the network again", OSD_COLOR_GREY);
      }
      ui_button(0, 14, BTN_WIDTH, "OK", true);
      ui_hint("Enter: back", "");
      break;
    }
  }
  osd_update();
}

// ---------------- screen ----------------

void app_wifi_overlay() {
  ui_screen("Setup Wi-Fi");

  hid_kbd_report_sync();
  prev_kbd = usb_keyboard_report; // the key that opened the screen is not a press here
  ws_set_list(LIST_ROWS, false);
  ws_start();
  draw();
}

static void on_key(uint8_t key, bool shift, bool joy) {
  if (ws_key(key, shift, joy)) {
    draw();
  } else {
    ws_stop();
    osd_state = state_setup;
  }
}

void app_wifi_on_keyboard() {
  uint8_t keys[6];
  uint8_t n = ui_pressed_keys(&prev_kbd, keys);
  bool shift = usb_keyboard_report.modifier & (KEY_MOD_LSHIFT | KEY_MOD_RSHIFT);
  for (uint8_t i = 0; i < n && osd_state == state_wifi; i++) on_key(keys[i], shift, false);

  // the joystick moves through the lists and buttons, picks the chars of the text fields
  uint8_t key = n == 0 && osd_state == state_wifi ? ui_joy_key() : 0;
  if (key) on_key(key, false, true);
}
