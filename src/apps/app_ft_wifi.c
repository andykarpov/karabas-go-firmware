#include "config.h"
#include "types.h"
#include "main.h"
#include "ft812.h"
#include "input/usb_hid_keys.h"
#include "periph/esp_at.h"
#include "ui/ft_mouse.h"
#include "ui/ft_page.h"
#include "apps/app_ft_pages.h"
#include "apps/wifi_setup.h"

// The Wi-Fi page of the graphic setup: apps/wifi_setup (shared with Setup Wi-Fi of the text menu)
// asks the module, scans and joins; this page draws it. The status is asked as soon as the tab is
// chosen.

// the clickable areas
enum { HIT_STATUS = 0, HIT_RETRY = 10, HIT_RESCAN, HIT_NEXT, HIT_CONNECT, HIT_SHOW, HIT_OK, HIT_FORGET, HIT_CANCEL, HIT_UPDATE, HIT_AP = 100 };

#define INFO_STEP 28
#define INFO_X 140
#define BTN_W 220
#define BTN_STEP 40
#define LIST_ROW_H 30
#define CHAR_W 13
#define CURSOR_COLOR 0x00d8d8
#define BAR_H 20
#define LOCK_W 12

static bool closed; // Esc left the page

static bool tick() { return ws_tick(); }

// ---------------- drawing ----------------

static void draw_info(const ft_page_ctx_t *c, int16_t y, const char *label, const char *value, uint32_t color) {
  ft_draw_text(c->x + 16, y, FT_PAGE_FONT, c->s->color_text, FT81x_OPT_CENTERY, label);
  ft_page_text(c->x + INFO_X, y, FT_PAGE_FONT, color, FT81x_OPT_CENTERY, value, (c->w - INFO_X - 16) / 12);
}

static void draw_message(const ft_page_ctx_t *c, int16_t y, const char *msg, uint32_t color) {
  ft_page_text(c->x + 16, y, FT_PAGE_FONT, color, FT81x_OPT_CENTERY, msg, 40);
}

// the text field: one char per CHAR_W (the fonts are not monospaced), the tail that fits,
// a cyan cursor at the end
static void draw_field(const ft_page_ctx_t *c, int16_t y, const char *text, bool hidden) {
  int16_t fx = c->x + 16, fw = c->w - 32;
  uint8_t width = (fw - 16) / CHAR_W;
  size_t len = strlen(text);
  size_t from = len >= width ? len - width + 1 : 0;
  ft_page_rect(fx - 2, y - 2, fw + 4, FT_PAGE_FIELD_H + 4, 4, c->s->color_active);
  ft_page_rect(fx, y, fw, FT_PAGE_FIELD_H, 4, c->s->color_bg);
  for (size_t i = from; i <= len; i++) {
    int16_t cx = fx + 8 + (i - from) * CHAR_W;
    if (i == len) {
      ft_draw_rect(cx, y + 4, CHAR_W, FT_PAGE_FIELD_H - 8, 1, CURSOR_COLOR);
      break;
    }
    char ch[2] = { hidden ? '*' : text[i], '\0' };
    if (ch[0] < 32 || ch[0] >= 127) ch[0] = '?';
    ft_draw_text(cx + CHAR_W / 2, y + FT_PAGE_FIELD_H / 2, FT_PAGE_FONT_SMALL, c->s->color_text, FT81x_OPT_CENTER, ch);
  }
}

static void draw_status(const ft_page_ctx_t *c, int16_t y) {
  const ws_t *w = ws_state();
  uint32_t value = c->s->color_text, dim = c->s->color_copyright;
  draw_info(c, y + INFO_STEP / 2, "Module", w->version[0] ? w->version : "?", value);
  draw_info(c, y + INFO_STEP * 3 / 2, "CTS", w->cts == WS_CTS_ON ? "on" : w->cts == WS_CTS_OFF ? "could not turn on" : "unknown",
    w->cts == WS_CTS_ON ? value : FT_PAGE_ERROR);
  draw_info(c, y + INFO_STEP * 5 / 2, "Network", w->cur_ssid[0] ? w->cur_ssid : "not connected", w->cur_ssid[0] ? value : dim);
  draw_info(c, y + INFO_STEP * 7 / 2, "IP", w->ip[0] && strcmp(w->ip, "0.0.0.0") != 0 ? w->ip : "-", value);
}

// a padlock LOCK_W wide, centered on y: closed for a network with a password, open (the right
// end of the shackle out of the body) for one without
static void draw_lock(int16_t x, int16_t y, bool closed, uint32_t color) {
  ft_page_rect(x, y - 1, LOCK_W, 8, 2, color);                 // the body
  ft_page_rect(x + 2, y - 8, 2, 8, 1, color);                  // the shackle: left
  ft_page_rect(x + 2, y - 8, LOCK_W - 4, 2, 1, color);         // top
  ft_page_rect(x + LOCK_W - 4, y - 8, 2, closed ? 8 : 4, 1, color); // right
}

static void draw_list(const ft_page_ctx_t *c) {
  const ws_t *w = ws_state();
  ft_draw_text(c->x + 16, c->y + FT_PAGE_BTN_H / 2, FT_PAGE_FONT_SMALL, c->s->color_copyright, FT81x_OPT_CENTERY, "dBm      Network");
  if (w->aps_len > 0) {
    char b[8];
    snprintf(b, sizeof(b), "%u/%u", w->sel + 1, w->aps_len);
    ft_draw_text(c->x + c->w - 16 - 140 - 16, c->y + FT_PAGE_BTN_H / 2, FT_PAGE_FONT_SMALL, c->s->color_copyright, FT81x_OPT_RIGHTX | FT81x_OPT_CENTERY, b);
  }
  ft_page_button(c, c->x + c->w - 16 - 140, c->y, 140, "Rescan", c->focused && (w->on_rescan || w->aps_len == 0), HIT_RESCAN);
  int16_t y0 = c->y + FT_PAGE_BTN_H + 8;
  uint8_t rows = (c->h - (y0 - c->y)) / LIST_ROW_H;
  if (rows < 1) rows = 1;
  ws_set_list(rows, true);
  if (w->aps_len == 0) {
    draw_message(c, y0 + LIST_ROW_H / 2, "No networks found", c->s->color_text);
    return;
  }
  uint8_t top = ws_list_top();
  for (uint8_t row = 0; row < rows && top + row < w->aps_len; row++) {
    const kg_wifi_ap_t *ap = &w->aps[top + row];
    int16_t y = y0 + row * LIST_ROW_H;
    bool hovered = ft_page_hit(c->x, y, c->w, LIST_ROW_H - 2, HIT_AP + top + row);
    ft_page_row(c, c->x, y, c->w, LIST_ROW_H - 2, top + row == w->sel && !w->on_rescan, hovered);
    char b[8];
    snprintf(b, sizeof(b), "%d", ap->rssi);
    int16_t cy = y + (LIST_ROW_H - 2) / 2;
    ft_draw_text(c->x + 60, cy, FT_PAGE_FONT, c->s->color_text, FT81x_OPT_RIGHTX | FT81x_OPT_CENTERY, b);
    bool joined = strcmp(ap->ssid, w->cur_ssid) == 0;
    uint32_t color = joined ? c->s->color_active : c->s->color_text;
    draw_lock(c->x + 72, cy, ap->ecn != 0, color);
    ft_page_text(c->x + 96, cy, FT_PAGE_FONT, color, FT81x_OPT_CENTERY, ap->ssid, (c->w - 112) / 12);
  }
}

static void draw(const ft_page_ctx_t *c) {
  const ws_t *w = ws_state();
  int16_t x = c->x + 16;
  uint32_t text = c->s->color_text, dim = c->s->color_copyright;
  char buf[48];

  switch (w->screen) {
    case WS_INIT:
      draw_message(c, c->y + INFO_STEP / 2, "Talking to ESP8266...", text);
      break;
    case WS_NO_ESP:
      draw_message(c, c->y + INFO_STEP / 2, "ESP8266 is not responding", FT_PAGE_ERROR);
      ft_page_button(c, x, c->y + 2 * INFO_STEP, BTN_W, "Retry", true, HIT_RETRY);
      break;
    case WS_STATUS: {
      draw_status(c, c->y);
      int16_t y = c->y + 4 * INFO_STEP + 16;
      // two columns: five buttons do not fit one under the other in 640x480
      uint8_t ids[WS_M_COUNT], n = ws_menu_items(ids), rows = (n + 1) / 2;
      int16_t bw = (c->w - 32 - 16) / 2 < BTN_W ? (c->w - 32 - 16) / 2 : BTN_W;
      for (uint8_t i = 0; i < n; i++) {
        ft_page_button(c, x + (i / rows) * (bw + 16), y + (i % rows) * BTN_STEP, bw, ws_menu_names[ids[i]], w->sel == i, HIT_STATUS + i);
      }
      break;
    }
    case WS_SCAN:
      draw_message(c, c->y + INFO_STEP / 2, "Scanning...", text);
      break;
    case WS_LIST:
      draw_list(c);
      break;
    case WS_SSID:
      draw_message(c, c->y + INFO_STEP / 2, "Network name:", text);
      draw_field(c, c->y + INFO_STEP + 4, w->ssid, false);
      ft_page_button(c, x, c->y + INFO_STEP + 4 + BTN_STEP + 8, 140, "Next", false, HIT_NEXT);
      break;
    case WS_PASS:
      draw_info(c, c->y + INFO_STEP / 2, "Network", w->ssid, text);
      draw_message(c, c->y + INFO_STEP * 3 / 2, "Password:", text);
      draw_field(c, c->y + 2 * INFO_STEP + 4, w->pass, !w->show_pass);
      ft_page_button(c, x, c->y + 2 * INFO_STEP + 4 + BTN_STEP + 8, 140, "Connect", false, HIT_CONNECT);
      ft_page_button(c, x + 156, c->y + 2 * INFO_STEP + 4 + BTN_STEP + 8, 140, w->show_pass ? "Hide" : "Show", false, HIT_SHOW);
      break;
    case WS_JOIN:
      snprintf(buf, sizeof(buf), "Connecting to %s...", w->ssid);
      draw_message(c, c->y + INFO_STEP / 2, buf, text);
      break;
    case WS_RESULT:
      if (w->result_error) {
        draw_message(c, c->y + INFO_STEP / 2, w->result_error, FT_PAGE_ERROR);
      } else {
        draw_message(c, c->y + INFO_STEP / 2, "Connected", FT_PAGE_OK);
        draw_info(c, c->y + INFO_STEP * 3 / 2, "Network", w->cur_ssid, text);
        draw_info(c, c->y + INFO_STEP * 5 / 2, "IP", w->ip[0] ? w->ip : "-", text);
        draw_message(c, c->y + INFO_STEP * 7 / 2, "The ESP8266 remembers it", dim);
      }
      ft_page_button(c, x, c->y + 4 * INFO_STEP + 16, 140, "OK", true, HIT_OK);
      break;
    case WS_FORGET:
      draw_message(c, c->y + INFO_STEP / 2, "Forget this network?", text);
      draw_info(c, c->y + INFO_STEP * 3 / 2, "Network", w->cur_ssid, text);
      draw_message(c, c->y + INFO_STEP * 5 / 2, "The ESP8266 will not join it", dim);
      ft_page_button(c, x, c->y + 3 * INFO_STEP + 16, 140, "Forget", w->sel == 0, HIT_FORGET);
      ft_page_button(c, x + 156, c->y + 3 * INFO_STEP + 16, 140, "Cancel", w->sel == 1, HIT_CANCEL);
      break;
    case WS_UPD_ASK:
      if (w->restoring) {
        draw_message(c, c->y + INFO_STEP / 2, "Erase the ESP8266 settings?", text);
        draw_message(c, c->y + INFO_STEP * 3 / 2, "For modules that lose settings", dim);
        draw_message(c, c->y + INFO_STEP * 5 / 2, "The network is forgotten too,", dim);
        draw_message(c, c->y + INFO_STEP * 7 / 2, "join it again after it", dim);
      } else {
        draw_message(c, c->y + INFO_STEP / 2, "Update the ESP8266 firmware?", text);
        draw_info(c, c->y + INFO_STEP * 3 / 2, "Module", w->version[0] ? w->version : "?", text);
        draw_message(c, c->y + INFO_STEP * 5 / 2, "From the Espressif server, ~30 s", dim);
        draw_message(c, c->y + INFO_STEP * 7 / 2, "Do not power off the board", dim);
      }
      ft_page_button(c, x, c->y + 4 * INFO_STEP + 16, 140, w->restoring ? "Erase" : "Update", w->sel == 0, HIT_UPDATE);
      ft_page_button(c, x + 156, c->y + 4 * INFO_STEP + 16, 140, "Cancel", w->sel == 1, HIT_CANCEL);
      break;
    case WS_UPD: {
      uint8_t percent = esp_update_progress();
      const char *msg = esp_update_message();
      draw_message(c, c->y + INFO_STEP / 2, msg ? msg : "", text);
      int16_t bw = c->w - 32 - 64, by = c->y + INFO_STEP + 8;
      ft_page_rect(x, by, bw, BAR_H, 4, dim);
      ft_page_rect(x + 2, by + 2, bw - 4, BAR_H - 4, 3, c->s->color_bg);
      if (percent > 0) ft_page_rect(x + 2, by + 2, (bw - 4) * percent / 100, BAR_H - 4, 3, c->s->color_active);
      snprintf(buf, sizeof(buf), "%u%%", percent);
      ft_draw_text(x + bw + 56, by + BAR_H / 2, FT_PAGE_FONT, text, FT81x_OPT_RIGHTX | FT81x_OPT_CENTERY, buf);
      draw_message(c, c->y + INFO_STEP * 3 + 4, "Do not power off the board", dim);
      break;
    }
    case WS_UPD_END: {
      bool ok = esp_update_poll() == ESP_UPD_OK;
      draw_message(c, c->y + INFO_STEP / 2, esp_update_message(), ok ? FT_PAGE_OK : FT_PAGE_ERROR);
      if (!w->restoring) {
        draw_info(c, c->y + INFO_STEP * 3 / 2, ok ? "Was" : "Module", w->old_version[0] ? w->old_version : "?", text);
        if (ok) draw_info(c, c->y + INFO_STEP * 5 / 2, "Now", esp_update_version()[0] ? esp_update_version() : "?", text);
      } else if (ok) {
        draw_message(c, c->y + INFO_STEP * 3 / 2, "Join the network again", dim);
      }
      ft_page_button(c, x, c->y + 4 * INFO_STEP + 16, 140, "OK", true, HIT_OK);
      break;
    }
  }
}

// ---------------- keys ----------------

// the tab is chosen: ask the module
static void page_show() { ws_start(); }

// the module goes on with an update by itself
static void page_hide() { ws_stop(); esp_update_cancel(); }

// the keys: the status is there already (or on its way)
static void open() {
  closed = false;
  if (ws_state()->screen == WS_NO_ESP) ws_start();
}

static void close() {}

static bool key(uint8_t key, bool shift, bool joy) {
  closed = !ws_key(key, shift, joy);
  return !closed;
}

static bool click(int16_t id) {
  if (id >= HIT_AP && id < HIT_AP + WS_AP_MAX) ws_click_ap(id - HIT_AP);
  else if (id >= HIT_STATUS && id < HIT_STATUS + WS_M_COUNT) ws_menu(id - HIT_STATUS);
  else {
    switch (id) {
      case HIT_RETRY: ws_start(); break;
      case HIT_RESCAN: ws_scan(); break;
      case HIT_NEXT: ws_next(); break;
      case HIT_CONNECT: ws_join(); break;
      case HIT_SHOW: ws_toggle_pass(); break;
      case HIT_OK: ws_ok(); break;
      case HIT_CANCEL: ws_confirm(false); break;
      case HIT_UPDATE: case HIT_FORGET: ws_confirm(true); break;
    }
  }
  ws_drawn(); // the setup draws after a click
  return true;
}

static void wheel(int16_t id, int d) { ws_wheel(d); }

static const char *hint() {
  switch (ws_state()->screen) {
    case WS_INIT: case WS_SCAN: case WS_JOIN: return "Please wait";
    case WS_UPD: return "Please wait, do not power off the board";
    case WS_UPD_ASK: return ws_state()->restoring ? "AT+RESTORE: the network, the mode and the UART settings" : "The module keeps its old firmware if the download fails";
    case WS_LIST: return "Closed lock: password, joined in color";
    case WS_SSID: return "Pad: Up/Down: char   Right: add   Left: delete";
    case WS_PASS: return ws_state()->show_pass ? "Tab: hide the password" : "Tab: show the password";
    default: return "";
  }
}

static const char *keys() {
  switch (ws_state()->screen) {
    case WS_LIST: return "Up/Down: select   Enter: connect   R: rescan   Esc: back";
    case WS_SSID: return "Enter: next   Esc: back";
    case WS_PASS: return "Enter: connect   Esc: back";
    case WS_FORGET: case WS_UPD_ASK: return "Left/Right: select   Enter: OK   Esc: back";
    case WS_JOIN: case WS_UPD: return "";
    case WS_UPD_END: return "Enter: OK";
    default: return "Up/Down: select   Enter: OK   Esc: back";
  }
}

const ft_page_t ft_page_wifi = { open, close, page_show, page_hide, draw, key, tick, click, wheel, hint, keys };
