#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "FT812.h"
#include "usb_hid_keys.h"
#include "esp_at_parse.h"
#include "hid_ascii.h"
#include "periph/esp_at.h"
#include "ui/ft_mouse.h"
#include "ui/ft_page.h"
#include "app_ft_pages.h"

// The Wi-Fi page of the graphic setup (the core browser is the BOOT core, which bridges the
// ESP8266 UART), as Setup Wi-Fi of the text menu: the module status, a scan, the password of
// the chosen network, the join. The ESP keeps the network in its flash and joins it on start
// (CWAUTOCONN); Forget turns that off and leaves the network. The status is asked as soon as the
// tab is chosen, and the hardware flow control of the ESP is turned on then: the cores stop it
// with CTS when their receive fifo is nearly full.

enum { W_INIT, W_NO_ESP, W_STATUS, W_SCAN, W_LIST, W_SSID, W_PASS, W_JOIN, W_RESULT, W_FORGET };

// the AT steps, run one after the other from the tick
enum { S_NONE, S_ECHO_OFF, S_VERSION, S_FLOW_GET, S_FLOW_SET, S_MODE_GET, S_MODE_SET, S_JAP_GET, S_IP_GET, S_SCAN, S_JOIN, S_AUTOCONN, S_UNAUTO, S_QAP };

enum { CTS_UNKNOWN, CTS_ON, CTS_OFF };

// the clickable areas
enum { HIT_STATUS = 0, HIT_RETRY = 10, HIT_RESCAN, HIT_NEXT, HIT_CONNECT, HIT_SHOW, HIT_OK, HIT_FORGET, HIT_CANCEL, HIT_AP = 100 };

#define AP_MAX 32
#define PASS_MAX 64
#define ECHO_TRIES 3
#define INFO_STEP 28
#define INFO_X 140
#define BTN_W 220
#define BTN_STEP 40
#define LIST_ROW_H 30
#define CHAR_W 13
#define CURSOR_COLOR 0x00d8d8

static uint8_t screen;
static uint8_t step;
static bool step_started;
static uint8_t tries;
static bool joining;   // the status steps after a join end on the result screen
static bool from_list; // the password screen was opened from the list (not for a typed ssid)
static bool dirty;     // the screen changed in the tick
static bool closed;    // Esc left the page
static uint8_t sel;
static uint8_t top;
static uint8_t list_rows = 8; // the rows that fit, from the last draw

static char version[16];
static uint8_t cts;
static int mode;
static int flow;
static char cur_ssid[KG_WIFI_SSID_MAX + 1];
static char ip[16];
static kg_wifi_ap_t aps[AP_MAX];
static uint8_t aps_len;

static char ssid[KG_WIFI_SSID_MAX + 1];
static char pass[PASS_MAX + 1];
static bool show_pass;
static int join_err;
static const char *result_error;

// ---------------- AT steps ----------------

static void on_version(const char *line) { kg_esp_parse_version(line, version, sizeof(version)); }
static void on_flow(const char *line) { int f = kg_esp_parse_flow(line); if (f >= 0) flow = f; }
static void on_mode(const char *line) { int m = kg_esp_parse_mode(line); if (m >= 0) mode = m; }
static void on_jap(const char *line) { kg_esp_parse_cwjap(line, cur_ssid, sizeof(cur_ssid)); }
static void on_ip(const char *line) { kg_esp_parse_ip(line, ip, sizeof(ip)); }
static void on_join(const char *line) { int e = kg_esp_join_error(line); if (e >= 0) join_err = e; }

static void on_scan(const char *line) {
  kg_wifi_ap_t ap;
  if (kg_esp_parse_cwlap(line, &ap)) kg_wifi_ap_add(aps, &aps_len, AP_MAX, &ap);
}

static void run(uint8_t s) {
  step = s;
  step_started = false;
}

static void start_step() {
  switch (step) {
    case S_ECHO_OFF: esp_at_start("ATE0", 1000, nullptr); break;
    case S_VERSION:  version[0] = '\0'; esp_at_start("AT+GMR", 2000, on_version); break;
    case S_FLOW_GET: flow = -1; esp_at_start("AT+UART_DEF?", 1000, on_flow); break;
    case S_FLOW_SET: esp_at_start("AT+UART_DEF=115200,8,1,0,2", 1000, nullptr); break;
    case S_MODE_GET: mode = -1; esp_at_start("AT+CWMODE?", 1000, on_mode); break;
    case S_MODE_SET: esp_at_start("AT+CWMODE=1", 1000, nullptr); break;
    case S_JAP_GET:  cur_ssid[0] = '\0'; esp_at_start("AT+CWJAP?", 2000, on_jap); break;
    case S_IP_GET:   ip[0] = '\0'; esp_at_start("AT+CIFSR", 2000, on_ip); break;
    case S_SCAN:     aps_len = 0; esp_at_start("AT+CWLAP", 10000, on_scan); break;
    case S_JOIN: {
      char q_ssid[2 * KG_WIFI_SSID_MAX + 1], q_pass[2 * PASS_MAX + 1], cmd[16 + sizeof(q_ssid) + sizeof(q_pass)];
      kg_esp_quote(ssid, q_ssid, sizeof(q_ssid));
      kg_esp_quote(pass, q_pass, sizeof(q_pass));
      snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", q_ssid, q_pass);
      join_err = -1;
      esp_at_start(cmd, 20000, on_join);
      break;
    }
    case S_AUTOCONN: esp_at_start("AT+CWAUTOCONN=1", 1000, nullptr); break;
    case S_UNAUTO:   esp_at_start("AT+CWAUTOCONN=0", 1000, nullptr); break;
    case S_QAP:      esp_at_start("AT+CWQAP", 2000, nullptr); break;
  }
  step_started = true;
}

static void show(uint8_t s) {
  screen = s;
  sel = 0;
  top = 0;
  dirty = true;
}

static void step_done(uint8_t r) {
  switch (step) {
    case S_ECHO_OFF:
      if (r == ESP_AT_OK) { run(S_VERSION); break; }
      if (++tries < ECHO_TRIES) { run(S_ECHO_OFF); break; }
      run(S_NONE);
      show(W_NO_ESP);
      break;
    case S_VERSION:
      run(S_FLOW_GET);
      break;
    case S_FLOW_GET:
      if (r != ESP_AT_OK || flow < 0) { cts = CTS_UNKNOWN; run(S_MODE_GET); }
      else if (flow & 2) { cts = CTS_ON; run(S_MODE_GET); }
      else run(S_FLOW_SET);
      break;
    case S_FLOW_SET:
      cts = r == ESP_AT_OK ? CTS_ON : CTS_OFF;
      run(S_MODE_GET);
      break;
    case S_MODE_GET:
      run(mode == 1 || mode == 3 ? S_JAP_GET : S_MODE_SET);
      break;
    case S_MODE_SET:
      run(S_JAP_GET);
      break;
    case S_JAP_GET:
      run(S_IP_GET);
      break;
    case S_IP_GET:
      run(S_NONE);
      if (joining) {
        joining = false;
        result_error = nullptr;
        show(W_RESULT);
      } else {
        show(W_STATUS);
      }
      break;
    case S_SCAN:
      run(S_NONE);
      if (r == ESP_AT_OK) {
        show(W_LIST);
      } else {
        result_error = "Scan failed";
        show(W_RESULT);
      }
      break;
    case S_JOIN:
      if (r == ESP_AT_OK) {
        run(S_AUTOCONN);
      } else {
        run(S_NONE);
        result_error = kg_esp_join_error_text(r == ESP_AT_TIMEOUT ? 1 : join_err);
        show(W_RESULT);
      }
      break;
    case S_AUTOCONN:
      joining = true;
      run(S_JAP_GET);
      break;
    case S_UNAUTO:
      run(S_QAP);
      break;
    case S_QAP:
      run(S_JAP_GET);
      break;
  }
}

static bool tick() {
  if (step != S_NONE) {
    if (!step_started) start_step();
    else {
      uint8_t r = esp_at_poll();
      if (r != ESP_AT_BUSY) step_done(r);
    }
  }
  bool d = dirty;
  dirty = false;
  return d;
}

// ---------------- drawing ----------------

static void draw_info(const ft_page_ctx_t &c, int16_t y, const char *label, const char *value, uint32_t color) {
  ft.drawText(c.x + 16, y, FT_PAGE_FONT, c.s->color_text, FT81x_OPT_CENTERY, label);
  ft_page_text(c.x + INFO_X, y, FT_PAGE_FONT, color, FT81x_OPT_CENTERY, value, (c.w - INFO_X - 16) / 12);
}

static void draw_message(const ft_page_ctx_t &c, int16_t y, const char *msg, uint32_t color) {
  ft_page_text(c.x + 16, y, FT_PAGE_FONT, color, FT81x_OPT_CENTERY, msg);
}

// the text field: one char per CHAR_W (the fonts are not monospaced), the tail that fits,
// a cyan cursor at the end
static void draw_field(const ft_page_ctx_t &c, int16_t y, const char *text, bool hidden) {
  int16_t fx = c.x + 16, fw = c.w - 32;
  uint8_t width = (fw - 16) / CHAR_W;
  size_t len = strlen(text);
  size_t from = len >= width ? len - width + 1 : 0;
  ft_page_rect(fx - 2, y - 2, fw + 4, FT_PAGE_FIELD_H + 4, 4, c.s->color_active);
  ft_page_rect(fx, y, fw, FT_PAGE_FIELD_H, 4, c.s->color_bg);
  for (size_t i = from; i <= len; i++) {
    int16_t cx = fx + 8 + (i - from) * CHAR_W;
    if (i == len) {
      ft.drawRect(cx, y + 4, CHAR_W, FT_PAGE_FIELD_H - 8, 1, CURSOR_COLOR);
      break;
    }
    char ch[2] = { hidden ? '*' : text[i], '\0' };
    if (ch[0] < 32 || ch[0] >= 127) ch[0] = '?';
    ft.drawText(cx + CHAR_W / 2, y + FT_PAGE_FIELD_H / 2, FT_PAGE_FONT_SMALL, c.s->color_text, FT81x_OPT_CENTER, ch);
  }
}

static void draw_status(const ft_page_ctx_t &c, int16_t y) {
  uint32_t value = c.s->color_text, dim = c.s->color_copyright;
  draw_info(c, y + INFO_STEP / 2, "Module", version[0] ? version : "?", value);
  draw_info(c, y + INFO_STEP * 3 / 2, "CTS", cts == CTS_ON ? "on" : cts == CTS_OFF ? "could not turn on" : "unknown",
    cts == CTS_ON ? value : FT_PAGE_ERROR);
  draw_info(c, y + INFO_STEP * 5 / 2, "Network", cur_ssid[0] ? cur_ssid : "not connected", cur_ssid[0] ? value : dim);
  draw_info(c, y + INFO_STEP * 7 / 2, "IP", ip[0] && strcmp(ip, "0.0.0.0") != 0 ? ip : "-", value);
}

static void draw_list(const ft_page_ctx_t &c) {
  ft.drawText(c.x + 16, c.y + FT_PAGE_BTN_H / 2, FT_PAGE_FONT_SMALL, c.s->color_copyright, FT81x_OPT_CENTERY, "dBm      Network");
  if (aps_len > 0) {
    char b[8];
    snprintf(b, sizeof(b), "%u/%u", sel + 1, aps_len);
    ft.drawText(c.x + c.w - 16 - 140 - 16, c.y + FT_PAGE_BTN_H / 2, FT_PAGE_FONT_SMALL, c.s->color_copyright, FT81x_OPT_RIGHTX | FT81x_OPT_CENTERY, b);
  }
  ft_page_button(c, c.x + c.w - 16 - 140, c.y, 140, "Rescan", false, HIT_RESCAN);
  int16_t y0 = c.y + FT_PAGE_BTN_H + 8;
  list_rows = (c.h - (y0 - c.y)) / LIST_ROW_H;
  if (list_rows < 1) list_rows = 1;
  if (aps_len == 0) {
    draw_message(c, y0 + LIST_ROW_H / 2, "No networks found", c.s->color_text);
    return;
  }
  if (sel < top) top = sel;
  if (sel >= top + list_rows) top = sel - list_rows + 1;
  for (uint8_t row = 0; row < list_rows && top + row < aps_len; row++) {
    const kg_wifi_ap_t &ap = aps[top + row];
    int16_t y = y0 + row * LIST_ROW_H;
    bool hovered = ft_page_hit(c.x, y, c.w, LIST_ROW_H - 2, HIT_AP + top + row);
    ft_page_row(c, c.x, y, c.w, LIST_ROW_H - 2, top + row == sel, hovered);
    char b[8];
    snprintf(b, sizeof(b), "%d", ap.rssi);
    int16_t cy = y + (LIST_ROW_H - 2) / 2;
    ft.drawText(c.x + 60, cy, FT_PAGE_FONT, c.s->color_text, FT81x_OPT_RIGHTX | FT81x_OPT_CENTERY, b);
    if (ap.ecn) ft.drawText(c.x + 76, cy, FT_PAGE_FONT, c.s->color_copyright, FT81x_OPT_CENTERY, "*");
    bool joined = strcmp(ap.ssid, cur_ssid) == 0;
    ft_page_text(c.x + 96, cy, FT_PAGE_FONT, joined ? c.s->color_active : c.s->color_text, FT81x_OPT_CENTERY, ap.ssid, (c.w - 112) / 12);
  }
}

static void draw(const ft_page_ctx_t &c) {
  int16_t x = c.x + 16;
  uint32_t text = c.s->color_text, dim = c.s->color_copyright;
  char buf[48];

  switch (screen) {
    case W_INIT:
      draw_message(c, c.y + INFO_STEP / 2, "Talking to ESP8266...", text);
      break;
    case W_NO_ESP:
      draw_message(c, c.y + INFO_STEP / 2, "ESP8266 is not responding", FT_PAGE_ERROR);
      ft_page_button(c, x, c.y + 2 * INFO_STEP, BTN_W, "Retry", true, HIT_RETRY);
      break;
    case W_STATUS: {
      draw_status(c, c.y);
      int16_t y = c.y + 4 * INFO_STEP + 16;
      ft_page_button(c, x, y, BTN_W, "Scan networks", sel == 0, HIT_STATUS + 0);
      ft_page_button(c, x, y + BTN_STEP, BTN_W, "Hidden network", sel == 1, HIT_STATUS + 1);
      if (cur_ssid[0]) ft_page_button(c, x, y + 2 * BTN_STEP, BTN_W, "Forget network", sel == 2, HIT_STATUS + 2);
      break;
    }
    case W_SCAN:
      draw_message(c, c.y + INFO_STEP / 2, "Scanning...", text);
      break;
    case W_LIST:
      draw_list(c);
      break;
    case W_SSID:
      draw_message(c, c.y + INFO_STEP / 2, "Network name:", text);
      draw_field(c, c.y + INFO_STEP + 4, ssid, false);
      ft_page_button(c, x, c.y + INFO_STEP + 4 + BTN_STEP + 8, 140, "Next", false, HIT_NEXT);
      break;
    case W_PASS:
      draw_info(c, c.y + INFO_STEP / 2, "Network", ssid, text);
      draw_message(c, c.y + INFO_STEP * 3 / 2, "Password:", text);
      draw_field(c, c.y + 2 * INFO_STEP + 4, pass, !show_pass);
      ft_page_button(c, x, c.y + 2 * INFO_STEP + 4 + BTN_STEP + 8, 140, "Connect", false, HIT_CONNECT);
      ft_page_button(c, x + 156, c.y + 2 * INFO_STEP + 4 + BTN_STEP + 8, 140, show_pass ? "Hide" : "Show", false, HIT_SHOW);
      break;
    case W_JOIN:
      snprintf(buf, sizeof(buf), "Connecting to %s...", ssid);
      draw_message(c, c.y + INFO_STEP / 2, buf, text);
      break;
    case W_RESULT:
      if (result_error) {
        draw_message(c, c.y + INFO_STEP / 2, result_error, FT_PAGE_ERROR);
      } else {
        draw_message(c, c.y + INFO_STEP / 2, "Connected", FT_PAGE_OK);
        draw_info(c, c.y + INFO_STEP * 3 / 2, "Network", cur_ssid, text);
        draw_info(c, c.y + INFO_STEP * 5 / 2, "IP", ip[0] ? ip : "-", text);
        draw_message(c, c.y + INFO_STEP * 7 / 2, "The ESP8266 remembers it", dim);
      }
      ft_page_button(c, x, c.y + 4 * INFO_STEP + 16, 140, "OK", true, HIT_OK);
      break;
    case W_FORGET:
      draw_message(c, c.y + INFO_STEP / 2, "Forget this network?", text);
      draw_info(c, c.y + INFO_STEP * 3 / 2, "Network", cur_ssid, text);
      draw_message(c, c.y + INFO_STEP * 5 / 2, "The ESP8266 will not join it", dim);
      ft_page_button(c, x, c.y + 3 * INFO_STEP + 16, 140, "Forget", sel == 0, HIT_FORGET);
      ft_page_button(c, x + 156, c.y + 3 * INFO_STEP + 16, 140, "Cancel", sel == 1, HIT_CANCEL);
      break;
  }
}

// ---------------- keys ----------------

static void start_init() {
  tries = 0;
  joining = false;
  cts = CTS_UNKNOWN;
  show(W_INIT);
  run(S_ECHO_OFF);
}

// the tab is chosen: ask the module
static void page_show() {
  start_init();
}

static void page_hide() { run(S_NONE); }

// the keys: the status is there already (or on its way)
static void open() {
  closed = false;
  sel = 0;
  if (screen == W_NO_ESP) start_init();
}

static void close() {}

static void back() {
  switch (screen) {
    case W_LIST: case W_SSID: case W_RESULT: case W_FORGET: show(W_STATUS); break;
    case W_PASS: show(from_list ? W_LIST : W_SSID); break;
    case W_JOIN: break; // the join finishes or times out
    default:
      closed = true;
  }
}

static void open_pass(const char *name, bool list) {
  strncpy(ssid, name, sizeof(ssid) - 1);
  ssid[sizeof(ssid) - 1] = '\0';
  pass[0] = '\0';
  from_list = list;
  show_pass = false;
  show(W_PASS);
}

static void join() {
  show(W_JOIN);
  run(S_JOIN);
}

static void scan() {
  show(W_SCAN);
  run(S_SCAN);
}

static void join_ap(uint8_t i) {
  sel = i;
  if (aps[i].ecn == 0) { strcpy(ssid, aps[i].ssid); pass[0] = '\0'; from_list = true; join(); }
  else open_pass(aps[i].ssid, true);
}

// a text field: typed chars, Backspace
static void edit(char *text, size_t max, uint8_t key, bool shift) {
  size_t len = strlen(text);
  if (key == KEY_BACKSPACE) {
    if (len > 0) text[len - 1] = '\0';
    return;
  }
  char c = kg_hid_ascii(key, shift);
  if (c && len < max) { text[len] = c; text[len + 1] = '\0'; }
}

static void status_enter() {
  if (sel == 0) scan();
  else if (sel == 1) { ssid[0] = '\0'; show(W_SSID); }
  else if (sel == 2) { screen = W_FORGET; sel = 1; } // Cancel first
}

static bool key(uint8_t key, bool shift, bool joy) {
  bool enter = key == KEY_ENTER || key == KEY_KPENTER;
  if (key == KEY_ESC) { back(); return !closed; }
  if (joy && (screen == W_SSID || screen == W_PASS)) return true; // the keyboard types there

  switch (screen) {
    case W_NO_ESP:
      if (enter) start_init();
      break;
    case W_STATUS: {
      uint8_t items = cur_ssid[0] ? 3 : 2;
      if (key == KEY_UP) sel = sel ? sel - 1 : items - 1;
      else if (key == KEY_DOWN || key == KEY_TAB) sel = (sel + 1) % items;
      else if (enter) status_enter();
      break;
    }
    case W_FORGET:
      if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_TAB) sel ^= 1;
      else if (enter && sel == 0) { show(W_INIT); run(S_UNAUTO); }
      else if (enter) show(W_STATUS);
      break;
    case W_LIST:
      if (key == KEY_UP && sel > 0) sel--;
      else if (key == KEY_DOWN && sel + 1 < aps_len) sel++;
      else if (key == KEY_PAGEUP) sel = sel > list_rows ? sel - list_rows : 0;
      else if (key == KEY_PAGEDOWN) sel = sel + list_rows < aps_len ? sel + list_rows : (aps_len ? aps_len - 1 : 0);
      else if (key == KEY_R) scan();
      else if (enter && aps_len) join_ap(sel);
      break;
    case W_SSID:
      if (enter) { if (ssid[0]) open_pass(ssid, false); }
      else edit(ssid, KG_WIFI_SSID_MAX, key, shift);
      break;
    case W_PASS:
      if (enter) join();
      else if (key == KEY_TAB) show_pass = !show_pass;
      else edit(pass, PASS_MAX, key, shift);
      break;
    case W_RESULT:
      if (enter) show(W_STATUS);
      break;
  }
  dirty = false; // the setup draws after a key
  return true;
}

static bool click(int16_t id) {
  if (id >= HIT_AP && id < HIT_AP + AP_MAX) {
    uint8_t i = id - HIT_AP;
    if (i == sel) join_ap(i);
    else sel = i;
  } else if (id >= HIT_STATUS && id < HIT_STATUS + 3) {
    sel = id - HIT_STATUS;
    status_enter();
  } else {
    switch (id) {
      case HIT_RETRY: start_init(); break;
      case HIT_RESCAN: scan(); break;
      case HIT_NEXT: if (ssid[0]) open_pass(ssid, false); break;
      case HIT_CONNECT: join(); break;
      case HIT_SHOW: show_pass = !show_pass; break;
      case HIT_OK: case HIT_CANCEL: show(W_STATUS); break;
      case HIT_FORGET: show(W_INIT); run(S_UNAUTO); break;
    }
  }
  dirty = false;
  return true;
}

static void wheel(int16_t id, int d) {
  if (screen != W_LIST || aps_len == 0) return;
  if (d > 0 && sel > 0) sel--;
  else if (d < 0 && sel + 1 < aps_len) sel++;
}

static const char *hint() {
  switch (screen) {
    case W_INIT: case W_SCAN: case W_JOIN: return "Please wait";
    case W_LIST: return "* password, joined in color";
    case W_PASS: return show_pass ? "Tab: hide the password" : "Tab: show the password";
    default: return "";
  }
}

static const char *keys() {
  switch (screen) {
    case W_LIST: return "Up/Down: select   Enter: connect   R: rescan   Esc: back";
    case W_SSID: return "Enter: next   Esc: back";
    case W_PASS: return "Enter: connect   Esc: back";
    case W_FORGET: return "Left/Right: select   Enter: OK   Esc: back";
    case W_JOIN: return "";
    default: return "Up/Down: select   Enter: OK   Esc: back";
  }
}

const ft_page_t ft_page_wifi = { open, close, page_show, page_hide, draw, key, tick, click, wheel, hint, keys };
