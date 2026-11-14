#include "config.h"
#include "types.h"
#include "main.h"
#include "apps/app_setup.h"
#include "osd.h"
#include "input/usb_hid_keys.h"
#include "input/sega.h"
#include "input/ps2kbd.h"
#include "periph/esp_at.h"
#include "apps/app_wifi.h"
#include "apps/wifi_setup.h"
#include "ui/ui_draw.h"
#include "ui/ui2.h"
#include "ui/osd2.h"
#include "periph/esp_update.h"

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

// ---- the bitmap OSD (link v2) ----

#define LIST2_ROWS (UI2_ROWS - 2) // the title of the list and the Rescan button

static uint8_t r2; // the next row
static void t2(const char *text, uint8_t flags) { ui2_text(r2++, text, flags); }
static void i2(const char *label, const char *value) { ui2_row(r2++, label, value, NULL, 0); }
static void b2(const char *a, const char *b, int focus) {
  const char *l[2] = { a, b };
  ui2_buttons(r2++, l, b ? 2 : 1, focus);
}
// a text field: the tail that fits (the cursor takes the last column) and a block cursor after it
static void f2(const char *text, bool hidden) {
  char t[UI2_FIELD_COLS + 2];
  size_t len = strlen(text), from = len > UI2_FIELD_COLS - 1 ? len - (UI2_FIELD_COLS - 1) : 0, k = 0;
  for (size_t i = from; i < len && k < sizeof(t) - 2; i++) t[k++] = hidden ? '*' : text[i];
  t[k] = ' ';
  t[k + 1] = '\0';
  ui2_field(r2++, "", t, (int) k, true, NULL);
}

static void draw2() {
  const ws_t *w = ws_state();
  char buf[48];
  const char *note = NULL;
  if (w->screen == WS_LIST && w->aps_len > LIST2_ROWS) {
    snprintf(buf, sizeof(buf), "%u/%u", w->sel + 1, w->aps_len);
    note = buf;
  } else if (w->screen == WS_PASS) {
    note = w->show_pass ? "Tab: hide" : "Tab: show";
  }
  ui2_screen_note("Setup Wi-Fi", note, true);
  r2 = 0;
  switch (w->screen) {
    case WS_INIT: ui2_center(1, 0); t2("Talking to ESP8266...", 0); break;
    case WS_NO_ESP: ui2_center(3, 0); t2("ESP8266 is not responding", 0); r2++; b2("Retry", NULL, 0); break;
    case WS_STATUS: {
      // the status, a separator, the menu: one page scrolled with the cursor (a joined module
      // has 5 items, 10 rows of the 9)
      char ip[20];
      snprintf(ip, sizeof(ip), "%s", w->ip[0] && strcmp(w->ip, "0.0.0.0") != 0 ? w->ip : "-");
      const char *info[4][2] = {
        { "Module", w->version[0] ? w->version : "?" },
        { "CTS", w->cts == WS_CTS_ON ? "on" : w->cts == WS_CTS_OFF ? "could not turn on" : "unknown" },
        { "Network", w->cur_ssid[0] ? w->cur_ssid : "not connected" },
        { "IP", ip },
      };
      uint8_t ids[WS_M_COUNT], n = ws_menu_items(ids);
      uint8_t total = 5 + n, cur = 5 + w->sel;
      uint8_t top = cur >= UI2_ROWS ? cur - UI2_ROWS + 1 : 0;
      for (uint8_t v = top; v < total && v - top < UI2_ROWS; v++) {
        uint8_t row = v - top;
        if (v < 4) ui2_row(row, info[v][0], info[v][1], NULL, 0);
        else if (v == 4) ui2_sep(row);
        else ui2_row(row, ws_menu_names[ids[v - 5]], NULL, NULL, (w->sel == v - 5 ? UI2_SEL : 0) | UI2_ARROW);
      }
      ui2_scrollbar(top, total);
      break;
    }
    case WS_SCAN: ui2_center(1, 0); t2("Scanning...", 0); break;
    case WS_LIST: {
      t2("   dBm   Network", UI2_TITLE);
      if (w->aps_len == 0) {
        t2("No networks found", 0);
      } else {
        uint8_t top = ws_list_top();
        for (uint8_t row = 0; row < LIST2_ROWS && top + row < w->aps_len; row++) {
          const kg_wifi_ap_t *ap = &w->aps[top + row];
          snprintf(buf, sizeof(buf), "%c %4d %c %s", strcmp(ap->ssid, w->cur_ssid) == 0 ? '>' : ' ', ap->rssi,
                   ap->ecn ? '*' : ' ', ap->ssid);
          ui2_text(1 + row, buf, top + row == w->sel && !w->on_rescan ? UI2_SEL : 0);
        }
        ui2_scrollbar(top, w->aps_len + 2); // under the title, above the button
      }
      r2 = UI2_ROWS - 1;
      b2("Rescan", NULL, w->on_rescan || w->aps_len == 0 ? 0 : -1);
      break;
    }
    case WS_SSID: ui2_center(2, 0); t2("Network name:", 0); f2(w->ssid, false); break;
    case WS_PASS: ui2_center(4, 0); i2("Network", w->ssid); r2++; t2("Password:", 0); f2(w->pass, !w->show_pass); break;
    case WS_JOIN: ui2_center(1, 0); snprintf(buf, sizeof(buf), "Connecting to %s...", w->ssid); t2(buf, 0); break;
    case WS_RESULT:
      if (w->result_error) {
        ui2_center(3, 0);
        t2(w->result_error, 0);
      } else {
        ui2_center(6, 0);
        t2("Connected", 0);
        i2("Network", w->cur_ssid);
        i2("IP", w->ip[0] ? w->ip : "-");
        t2("The ESP8266 remembers it", UI2_DIM);
      }
      r2++;
      b2("OK", NULL, 0);
      break;
    case WS_FORGET:
      ui2_center(5, 0);
      t2("Forget this network?", 0);
      i2("Network", w->cur_ssid);
      t2("The ESP8266 will not join it", UI2_DIM);
      r2++;
      b2("Forget", "Cancel", w->sel);
      break;
    case WS_UPD_ASK:
      if (w->restoring) {
        ui2_center(6, 0);
        t2("Erase the ESP8266 settings?", 0);
        t2("For modules that do not keep their", UI2_DIM);
        t2("settings. It forgets the network:", UI2_DIM);
        t2("join it again.", UI2_DIM);
      } else {
        ui2_center(6, 0);
        t2("Update the ESP8266 firmware?", 0);
        i2("Module", w->version[0] ? w->version : "?");
        t2("From the Espressif server, about half", UI2_DIM);
        t2("a minute. Do not power off the board.", UI2_DIM);
      }
      r2++;
      b2(w->restoring ? "Wipe" : "Update", "Cancel", w->sel);
      break;
    case WS_UPD: {
      const char *msg = esp_update_message();
      ui2_box_progress(msg ? msg : "Updating", esp_update_progress(), 100);
      break;
    }
    case WS_UPD_END: {
      bool ok = esp_update_poll() == ESP_UPD_OK;
      ui2_center(5, 0);
      t2(esp_update_message(), 0);
      if (!w->restoring) {
        i2(ok ? "Was" : "Module", w->old_version[0] ? w->old_version : "?");
        if (ok) i2("Now", esp_update_version()[0] ? esp_update_version() : "?");
      } else if (ok) {
        t2("Join the network again", UI2_DIM);
      }
      r2++;
      b2("OK", NULL, 0);
      break;
    }
  }
  ui2_done();
}

static void draw() {
  if (osd2_active()) { draw2(); return; }
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
  if (!osd2_active()) ui_screen("Setup Wi-Fi");

  hid_kbd_report_sync();
  prev_kbd = usb_keyboard_report; // the key that opened the screen is not a press here
  ws_set_list(osd2_active() ? LIST2_ROWS : LIST_ROWS, false);
  ws_start();
  draw();
}

static void on_key(uint8_t key, bool shift, bool joy) {
  if (ws_key(key, shift, joy)) {
    draw();
  } else {
    ws_stop();
    osd_state = app_setup_back();
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
