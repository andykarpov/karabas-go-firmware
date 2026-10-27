#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "OSD.h"
#include "usb_hid_keys.h"
#include "input/sega.h"
#include "ps2kbd.h"
#include "esp_at_parse.h"
#include "hid_ascii.h"
#include "periph/esp_at.h"
#include "app_wifi.h"

// Setup > Setup Wi-Fi (BOOT core only, it bridges the ESP8266 UART): the module status, a scan,
// the password of the chosen network, the join. The ESP keeps the network in its flash and joins
// it on start (CWAUTOCONN); Forget turns that off and leaves the network. On the way in, the hardware flow control of the ESP is turned on:
// the cores stop it with CTS when their receive fifo is nearly full. Update module (when joined):
// the AT firmware from Espressif's server (periph/esp_update); Reset module: AT+RESTORE,
// for modules whose saved settings got broken. The status is asked again after both.

enum { W_INIT, W_NO_ESP, W_STATUS, W_SCAN, W_LIST, W_SSID, W_PASS, W_JOIN, W_RESULT, W_FORGET, W_UPD_ASK, W_UPD, W_UPD_END };

// the AT steps, run one after the other from the tick
enum { S_NONE, S_ECHO_OFF, S_VERSION, S_FLOW_GET, S_FLOW_SET, S_MODE_GET, S_MODE_SET, S_JAP_GET, S_IP_GET, S_SCAN, S_JOIN, S_AUTOCONN, S_UNAUTO, S_QAP };

enum { CTS_UNKNOWN, CTS_ON, CTS_OFF };

#define AP_MAX 32
#define LIST_ROW 8
#define LIST_ROWS 14
#define PASS_MAX 64
#define ECHO_TRIES 3
#define BTN_WIDTH 10
#define MENU_WIDTH 18
#define ROW_RESCAN 22

static uint8_t screen;
static uint8_t step;
static bool step_started;
static uint8_t tries;
static bool joining;   // the status steps after a join end on the result screen
static bool from_list; // the password screen was opened from the list (not for a typed ssid)
static uint8_t sel;
static uint8_t top;

// the buttons of the status screen; Forget and Update only when joined
enum { M_SCAN, M_HIDDEN, M_FORGET, M_UPDATE, M_RESET, M_COUNT };
static const char *const menu_names[M_COUNT] = { "Scan networks", "Hidden network", "Forget network", "Update module", "Reset module" };
static bool restoring;       // the reset screens, not the update ones

static char version[16];
static char old_version[16]; // before the update
static uint8_t upd_percent;  // drawn last
static const char *upd_message;
static uint8_t cts;
static int mode;
static int flow;
static bool on_rescan; // the list: the Rescan button has the focus (below the last network)
static char cur_ssid[KG_WIFI_SSID_MAX + 1];
static char ip[16];
static kg_wifi_ap_t aps[AP_MAX];
static uint8_t aps_len;

static char ssid[KG_WIFI_SSID_MAX + 1];
static char pass[PASS_MAX + 1];
static bool show_pass;
static int join_err;
static const char *result_error;

static hid_keyboard_report_t prev_kbd;

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

static uint8_t menu_items(uint8_t *ids) {
  uint8_t n = 0;
  ids[n++] = M_SCAN;
  ids[n++] = M_HIDDEN;
  if (cur_ssid[0]) { ids[n++] = M_FORGET; ids[n++] = M_UPDATE; }
  ids[n++] = M_RESET;
  return n;
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

static void draw();

static void show(uint8_t s) {
  screen = s;
  sel = 0;
  on_rescan = false;
  top = 0;
  draw();
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

static void update_tick() {
  uint8_t r = esp_update_poll();
  if (r == ESP_UPD_BUSY) {
    uint8_t p = esp_update_progress();
    if (p != upd_percent || esp_update_message() != upd_message) draw();
    return;
  }
  show(W_UPD_END);
}

void app_wifi_tick() {
  if (screen == W_UPD) { update_tick(); return; }
  if (step == S_NONE) return;
  if (!step_started) { start_step(); return; }
  uint8_t r = esp_at_poll();
  if (r == ESP_AT_BUSY) return;
  step_done(r);
}

// ---------------- drawing ----------------

// s with the chars the OSD font has no glyph for as '?', at most width chars
static void print_text(const char *s, uint8_t width) {
  for (uint8_t i = 0; s[i] && i < width; i++) {
    char c = s[i];
    zxosd.print((c >= 32 && c < 127) ? c : '?');
  }
}

static void draw_info(uint8_t y, const char *label, const char *value, uint8_t color = OSD::COLOR_CYAN_I) {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.setPos(1, y); zxosd.print(label);
  zxosd.setColor(color, OSD::COLOR_BLACK);
  zxosd.setPos(10, y); print_text(value, 21);
}

static void draw_message(uint8_t y, const char *msg, uint8_t color) {
  zxosd.setColor(color, OSD::COLOR_BLACK);
  zxosd.setPos(1, y); print_text(msg, 30);
}

static void draw_footer(const char *line1, const char *line2) {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.fill(0, 24, 31, 25, ' ');
  zxosd.setPos(1, 24); zxosd.print(line1);
  zxosd.setPos(1, 25); zxosd.print(line2);
}

// the text field: the tail that fits, a cyan cursor at the end
static void draw_field(uint8_t y, const char *text, bool hidden) {
  const uint8_t width = 30;
  size_t len = strlen(text);
  size_t from = len >= width ? len - width + 1 : 0;
  zxosd.setColor(OSD::COLOR_BLACK, OSD::COLOR_WHITE);
  zxosd.setPos(1, y);
  for (size_t i = from; i < from + width; i++) {
    if (i == len) {
      zxosd.setColor(OSD::COLOR_BLACK, OSD::COLOR_CYAN_I);
      zxosd.print(' ');
      zxosd.setColor(OSD::COLOR_BLACK, OSD::COLOR_WHITE);
    } else if (i < len) {
      char c = hidden ? '*' : text[i];
      zxosd.print((c >= 32 && c < 127) ? c : '?');
    } else {
      zxosd.print(' ');
    }
  }
}

static void draw_list() {
  ui_button(1, ROW_RESCAN, BTN_WIDTH, "Rescan", on_rescan || aps_len == 0);
  if (aps_len == 0) {
    draw_message(LIST_ROW, "No networks found", OSD::COLOR_YELLOW_I);
    return;
  }
  if (sel < top) top = sel;
  if (sel >= top + LIST_ROWS) top = sel - LIST_ROWS + 1;
  for (uint8_t row = 0; row < LIST_ROWS && top + row < aps_len; row++) {
    const kg_wifi_ap_t &ap = aps[top + row];
    bool focused = top + row == sel && !on_rescan;
    zxosd.setColor(focused ? OSD::COLOR_BLACK : OSD::COLOR_WHITE, focused ? OSD::COLOR_YELLOW_I : OSD::COLOR_BLACK);
    zxosd.fill(0, LIST_ROW + row, 31, LIST_ROW + row, ' ');
    zxosd.setPos(0, LIST_ROW + row);
    zxosd.print(strcmp(ap.ssid, cur_ssid) == 0 ? '>' : ' ');
    zxosd.printf("%4d %c ", ap.rssi, ap.ecn ? '*' : ' ');
    print_text(ap.ssid, 24);
  }
  if (aps_len > LIST_ROWS) {
    zxosd.setColor(OSD::COLOR_GREY, OSD::COLOR_BLACK);
    zxosd.setPos(24, 7); zxosd.printf("%2u/%-2u", sel + 1, aps_len);
  }
}

static void draw() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.fill(0, 7, 31, 22, ' ');
  char buf[40];

  switch (screen) {
    case W_INIT:
      draw_message(8, "Talking to ESP8266...", OSD::COLOR_YELLOW_I);
      draw_footer("Please wait", "Esc: back");
      break;
    case W_NO_ESP:
      draw_message(8, "ESP8266 is not responding", OSD::COLOR_RED_I);
      ui_button(1, 11, BTN_WIDTH, "Retry", true);
      draw_footer("Enter: retry", "Esc: back");
      break;
    case W_STATUS:
      draw_info(8, "Module", version[0] ? version : "?");
      draw_info(9, "CTS", cts == CTS_ON ? "on" : cts == CTS_OFF ? "could not turn on" : "unknown",
                cts == CTS_ON ? OSD::COLOR_CYAN_I : OSD::COLOR_RED_I);
      draw_info(10, "Network", cur_ssid[0] ? cur_ssid : "not connected", cur_ssid[0] ? OSD::COLOR_CYAN_I : OSD::COLOR_GREY);
      draw_info(11, "IP", ip[0] && strcmp(ip, "0.0.0.0") != 0 ? ip : "-");
      {
        uint8_t ids[M_COUNT], n = menu_items(ids);
        for (uint8_t i = 0; i < n; i++) ui_button(1, 14 + 2 * i, MENU_WIDTH, menu_names[ids[i]], sel == i);
      }
      draw_footer("Enter: select", "Esc: back");
      break;
    case W_SCAN:
      draw_message(8, "Scanning...", OSD::COLOR_YELLOW_I);
      draw_footer("Please wait", "Esc: back");
      break;
    case W_LIST:
      zxosd.setColor(OSD::COLOR_GREY, OSD::COLOR_BLACK);
      zxosd.setPos(1, 7); zxosd.print("dBm   Network");
      draw_list();
      draw_footer("Enter: connect, R: rescan", "* password, > joined; Esc: back");
      break;
    case W_SSID:
      draw_message(8, "Network name:", OSD::COLOR_WHITE);
      draw_field(9, ssid, false);
      draw_footer("Enter: next", "Esc: back");
      break;
    case W_PASS:
      zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
      zxosd.setPos(1, 8); zxosd.print("Network:");
      zxosd.setColor(OSD::COLOR_CYAN_I, OSD::COLOR_BLACK);
      zxosd.setPos(10, 8); print_text(ssid, 21);
      draw_message(10, "Password:", OSD::COLOR_WHITE);
      draw_field(11, pass, !show_pass);
      draw_footer("Enter: connect, Esc: back", show_pass ? "Tab: hide the password" : "Tab: show the password");
      break;
    case W_JOIN:
      snprintf(buf, sizeof(buf), "Connecting to %s...", ssid);
      draw_message(8, buf, OSD::COLOR_YELLOW_I);
      draw_footer("Please wait", "");
      break;
    case W_RESULT:
      if (result_error) {
        draw_message(8, result_error, OSD::COLOR_RED_I);
      } else {
        draw_message(8, "Connected", OSD::COLOR_GREEN_I);
        draw_info(10, "Network", cur_ssid);
        draw_info(11, "IP", ip[0] ? ip : "-");
        draw_message(13, "The ESP8266 remembers it", OSD::COLOR_GREY);
      }
      ui_button(1, 16, BTN_WIDTH, "OK", true);
      draw_footer("Enter: back", "");
      break;
    case W_FORGET:
      draw_message(8, "Forget this network?", OSD::COLOR_YELLOW_I);
      draw_info(10, "Network", cur_ssid);
      draw_message(12, "The ESP8266 will not join it", OSD::COLOR_GREY);
      ui_button(1, 15, BTN_WIDTH, "Forget", sel == 0);
      ui_button(12, 15, BTN_WIDTH, "Cancel", sel == 1);
      draw_footer("Left/Right: select", "Enter: OK, Esc: back");
      break;
    case W_UPD_ASK:
      if (restoring) {
        draw_message(8, "Erase the ESP8266 settings?", OSD::COLOR_YELLOW_I);
        draw_message(10, "For modules that do not keep", OSD::COLOR_GREY);
        draw_message(11, "their settings. It forgets", OSD::COLOR_GREY);
        draw_message(12, "the network: join it again.", OSD::COLOR_GREY);
      } else {
        draw_message(8, "Update the ESP8266 firmware?", OSD::COLOR_YELLOW_I);
        draw_info(10, "Module", version[0] ? version : "?");
        draw_message(12, "From the Espressif server,", OSD::COLOR_GREY);
        draw_message(13, "about half a minute.", OSD::COLOR_GREY);
        draw_message(14, "Do not power off the board.", OSD::COLOR_GREY);
      }
      ui_button(1, 17, BTN_WIDTH, restoring ? "Erase" : "Update", sel == 0);
      ui_button(12, 17, BTN_WIDTH, "Cancel", sel == 1);
      draw_footer("Left/Right: select", "Enter: OK, Esc: back");
      break;
    case W_UPD:
      upd_percent = esp_update_progress();
      upd_message = esp_update_message();
      draw_message(8, upd_message ? upd_message : "", OSD::COLOR_YELLOW_I);
      zxosd.progress(1, 10, 26, upd_percent, 100, OSD::COLOR_CYAN_I, OSD::COLOR_BLACK);
      zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
      zxosd.setPos(28, 10); zxosd.printf("%3u%%", upd_percent);
      draw_message(12, "Do not power off the board", OSD::COLOR_GREY);
      draw_footer("Please wait", "");
      break;
    case W_UPD_END: {
      bool ok = esp_update_poll() == ESP_UPD_OK;
      draw_message(8, esp_update_message(), ok ? OSD::COLOR_GREEN_I : OSD::COLOR_RED_I);
      if (!restoring) {
        draw_info(10, ok ? "Was" : "Module", old_version[0] ? old_version : "?");
        if (ok) draw_info(11, "Now", esp_update_version()[0] ? esp_update_version() : "?");
      } else if (ok) {
        draw_message(10, "Join the network again", OSD::COLOR_GREY);
      }
      ui_button(1, 14, BTN_WIDTH, "OK", true);
      draw_footer("Enter: back", "");
      break;
    }
  }
  zxosd.update();
}

// ---------------- screen ----------------

static void start_init() {
  tries = 0;
  joining = false;
  cts = CTS_UNKNOWN;
  show(W_INIT);
  run(S_ECHO_OFF);
}

void app_wifi_overlay() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();
  zxosd.header(core.build, core.id, HW_ID);
  print_time();
  zxosd.setPos(0, 5);
  zxosd.print("Setup Wi-Fi");
  zxosd.line(6);
  zxosd.line(23);

  hid_kbd_report_sync();
  prev_kbd = usb_keyboard_report; // the key that opened the screen is not a press here
  start_init();
}

static void back() {
  switch (screen) {
    case W_LIST: case W_SSID: case W_RESULT: case W_FORGET: case W_UPD_ASK: show(W_STATUS); break;
    case W_PASS: show(from_list ? W_LIST : W_SSID); break;
    case W_JOIN: case W_UPD: break; // the join / the update finishes or times out
    case W_UPD_END: esp_update_cancel(); start_init(); break; // the new firmware may have lost the CTS
    default:
      run(S_NONE);
      osd_state = state_setup;
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

static void on_key(uint8_t key, bool shift) {
  bool enter = key == KEY_ENTER || key == KEY_KPENTER;
  // Left goes back too where it has nothing else to do
  bool left_back = key == KEY_LEFT && !ui_repeat && (screen == W_NO_ESP || screen == W_STATUS || screen == W_LIST || screen == W_RESULT);
  if (key == KEY_ESC || left_back) { back(); return; }

  switch (screen) {
    case W_NO_ESP:
      if (enter) start_init();
      break;
    case W_STATUS: {
      uint8_t ids[M_COUNT], items = menu_items(ids);
      uint8_t id = ids[sel < items ? sel : 0];
      if (key == KEY_UP) sel = sel ? sel - 1 : items - 1;
      else if (key == KEY_DOWN || key == KEY_TAB) sel = (sel + 1) % items;
      else if (enter && id == M_SCAN) { show(W_SCAN); run(S_SCAN); return; }
      else if (enter && id == M_HIDDEN) { ssid[0] = '\0'; show(W_SSID); return; }
      else if (enter && id == M_FORGET) { screen = W_FORGET; sel = 1; } // Cancel first
      else if (enter) { restoring = id == M_RESET; screen = W_UPD_ASK; sel = 1; }
      draw();
      break;
    }
    case W_UPD_ASK:
      if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_TAB) sel ^= 1;
      else if (enter && sel == 0) {
        strlcpy(old_version, version, sizeof(old_version));
        if (restoring) esp_restore_start(); else esp_update_start();
        show(W_UPD);
        return;
      }
      else if (enter) { show(W_STATUS); return; }
      draw();
      break;
    case W_UPD_END:
      if (enter) back();
      break;
    case W_FORGET:
      if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_TAB) sel ^= 1;
      else if (enter && sel == 0) { show(W_INIT); run(S_UNAUTO); return; }
      else if (enter) { show(W_STATUS); return; }
      draw();
      break;
    case W_LIST:
      if (key == KEY_UP && on_rescan) on_rescan = false;
      else if (key == KEY_UP && sel > 0) sel--;
      else if (key == KEY_DOWN && !on_rescan && sel + 1 < aps_len) sel++;
      else if (key == KEY_DOWN) on_rescan = true; // below the last network
      else if (key == KEY_PAGEUP) { on_rescan = false; sel = sel > LIST_ROWS ? sel - LIST_ROWS : 0; }
      else if (key == KEY_PAGEDOWN) { on_rescan = false; sel = sel + LIST_ROWS < aps_len ? sel + LIST_ROWS : (aps_len ? aps_len - 1 : 0); }
      else if (key == KEY_R || (enter && (on_rescan || aps_len == 0))) { show(W_SCAN); run(S_SCAN); return; }
      else if (enter) {
        if (aps[sel].ecn == 0) { strcpy(ssid, aps[sel].ssid); pass[0] = '\0'; from_list = true; join(); }
        else open_pass(aps[sel].ssid, true);
        return;
      }
      draw();
      break;
    case W_SSID:
      if (enter) { if (ssid[0]) open_pass(ssid, false); return; }
      edit(ssid, KG_WIFI_SSID_MAX, key, shift);
      draw();
      break;
    case W_PASS:
      if (enter) { join(); return; }
      if (key == KEY_TAB) show_pass = !show_pass;
      else edit(pass, PASS_MAX, key, shift);
      draw();
      break;
    case W_RESULT:
      if (enter) show(W_STATUS);
      break;
  }
}

void app_wifi_on_keyboard() {
  uint8_t keys[6];
  uint8_t n = ui_pressed_keys(&prev_kbd, keys);
  bool shift = usb_keyboard_report.modifier & (KEY_MOD_LSHIFT | KEY_MOD_RSHIFT);
  for (uint8_t i = 0; i < n && osd_state == state_wifi; i++) on_key(keys[i], shift);

  // the joystick moves through the lists and buttons, picks the chars of the text fields
  uint8_t key = n == 0 && osd_state == state_wifi ? ui_joy_key() : 0;
  if (!key) return;
  if (screen == W_SSID || screen == W_PASS) {
    bool p = screen == W_PASS;
    if (ui_joy_edit(p ? pass : ssid, p ? PASS_MAX : KG_WIFI_SSID_MAX, key)) {
      if (p) show_pass = true; // the picked chars are seen
      draw();
      return;
    }
  }
  on_key(key, false);
}
