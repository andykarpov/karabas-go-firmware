#include "main.h"
#include "input/usb_hid_keys.h"
#include "hid_ascii.h"
#include "periph/esp_at.h"
#include "apps/wifi_setup.h"

// the AT steps, run one after the other from the tick
enum { S_NONE, S_ECHO_OFF, S_VERSION, S_FLOW_GET, S_FLOW_SET, S_MODE_GET, S_MODE_SET, S_JAP_GET, S_IP_GET, S_SCAN, S_JOIN, S_AUTOCONN, S_UNAUTO, S_QAP };

#define ECHO_TRIES 3

const char *const ws_menu_names[WS_M_COUNT] = { "Network scan", "Hidden network", "Forget network", "Upgrade module", "Reset module" };

static ws_t ws;
static uint8_t step;
static bool step_started;
static uint8_t tries;
static bool joining;   // the status steps after a join end on the result screen
static bool from_list; // the password screen was opened from the list (not for a typed ssid)
static bool dirty;     // the screen changed in the tick
static int mode;
static int flow;
static int join_err;
static uint8_t top;
static uint8_t list_rows = 8;
static bool rescan_above;
static uint8_t upd_percent; // the progress seen last
static const char *upd_message;

const ws_t *ws_state(void) { return &ws; }

// ---------------- AT steps ----------------

static void on_version(const char *line) { kg_esp_parse_version(line, ws.version, sizeof(ws.version)); }
static void on_flow(const char *line) { int f = kg_esp_parse_flow(line); if (f >= 0) flow = f; }
static void on_mode(const char *line) { int m = kg_esp_parse_mode(line); if (m >= 0) mode = m; }
static void on_jap(const char *line) { kg_esp_parse_cwjap(line, ws.cur_ssid, sizeof(ws.cur_ssid)); }
static void on_ip(const char *line) { kg_esp_parse_ip(line, ws.ip, sizeof(ws.ip)); }
static void on_join(const char *line) { int e = kg_esp_join_error(line); if (e >= 0) join_err = e; }

static void on_scan(const char *line) {
  kg_wifi_ap_t ap;
  if (kg_esp_parse_cwlap(line, &ap)) kg_wifi_ap_add(ws.aps, &ws.aps_len, WS_AP_MAX, &ap);
}

uint8_t ws_menu_items(uint8_t *ids) {
  uint8_t n = 0;
  ids[n++] = WS_M_SCAN;
  ids[n++] = WS_M_HIDDEN;
  if (ws.cur_ssid[0]) { ids[n++] = WS_M_FORGET; ids[n++] = WS_M_UPDATE; }
  ids[n++] = WS_M_RESET;
  return n;
}

static void run(uint8_t s) {
  step = s;
  step_started = false;
}

static void start_step() {
  switch (step) {
    case S_ECHO_OFF: esp_at_start("ATE0", 1000, NULL); break;
    case S_VERSION:  ws.version[0] = '\0'; esp_at_start("AT+GMR", 2000, on_version); break;
    case S_FLOW_GET: flow = -1; esp_at_start("AT+UART_DEF?", 1000, on_flow); break;
    case S_FLOW_SET: esp_at_start("AT+UART_DEF=115200,8,1,0,2", 1000, NULL); break;
    case S_MODE_GET: mode = -1; esp_at_start("AT+CWMODE?", 1000, on_mode); break;
    case S_MODE_SET: esp_at_start("AT+CWMODE=1", 1000, NULL); break;
    case S_JAP_GET:  ws.cur_ssid[0] = '\0'; esp_at_start("AT+CWJAP?", 2000, on_jap); break;
    case S_IP_GET:   ws.ip[0] = '\0'; esp_at_start("AT+CIFSR", 2000, on_ip); break;
    case S_SCAN:     ws.aps_len = 0; esp_at_start("AT+CWLAP", 10000, on_scan); break;
    case S_JOIN: {
      char q_ssid[2 * KG_WIFI_SSID_MAX + 1], q_pass[2 * WS_PASS_MAX + 1], cmd[16 + sizeof(q_ssid) + sizeof(q_pass)];
      kg_esp_quote(ws.ssid, q_ssid, sizeof(q_ssid));
      kg_esp_quote(ws.pass, q_pass, sizeof(q_pass));
      snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", q_ssid, q_pass);
      join_err = -1;
      esp_at_start(cmd, 20000, on_join);
      break;
    }
    case S_AUTOCONN: esp_at_start("AT+CWAUTOCONN=1", 1000, NULL); break;
    case S_UNAUTO:   esp_at_start("AT+CWAUTOCONN=0", 1000, NULL); break;
    case S_QAP:      esp_at_start("AT+CWQAP", 2000, NULL); break;
  }
  step_started = true;
}

static void show(uint8_t s) {
  ws.screen = s;
  ws.sel = 0;
  ws.on_rescan = false;
  top = 0;
  dirty = true;
}

static void step_done(uint8_t r) {
  switch (step) {
    case S_ECHO_OFF:
      if (r == ESP_AT_OK) { run(S_VERSION); break; }
      if (++tries < ECHO_TRIES) { run(S_ECHO_OFF); break; }
      run(S_NONE);
      show(WS_NO_ESP);
      break;
    case S_VERSION:
      run(S_FLOW_GET);
      break;
    case S_FLOW_GET:
      if (r != ESP_AT_OK || flow < 0) { ws.cts = WS_CTS_UNKNOWN; run(S_MODE_GET); }
      else if (flow & 2) { ws.cts = WS_CTS_ON; run(S_MODE_GET); }
      else run(S_FLOW_SET);
      break;
    case S_FLOW_SET:
      ws.cts = r == ESP_AT_OK ? WS_CTS_ON : WS_CTS_OFF;
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
        ws.result_error = NULL;
        show(WS_RESULT);
      } else {
        show(WS_STATUS);
      }
      break;
    case S_SCAN:
      run(S_NONE);
      if (r == ESP_AT_OK) {
        show(WS_LIST);
      } else {
        ws.result_error = "Scan failed";
        show(WS_RESULT);
      }
      break;
    case S_JOIN:
      if (r == ESP_AT_OK) {
        run(S_AUTOCONN);
      } else {
        run(S_NONE);
        ws.result_error = kg_esp_join_error_text(r == ESP_AT_TIMEOUT ? 1 : join_err);
        show(WS_RESULT);
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

bool ws_tick(void) {
  if (ws.screen == WS_UPD) {
    if (esp_update_poll() != ESP_UPD_BUSY) {
      ws.screen = WS_UPD_END;
      dirty = true;
    } else if (esp_update_progress() != upd_percent || esp_update_message() != upd_message) {
      upd_percent = esp_update_progress();
      upd_message = esp_update_message();
      dirty = true;
    }
  } else if (step != S_NONE) {
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

void ws_drawn(void) { dirty = false; }

void ws_start(void) {
  tries = 0;
  joining = false;
  ws.cts = WS_CTS_UNKNOWN;
  show(WS_INIT);
  run(S_ECHO_OFF);
}

void ws_stop(void) { run(S_NONE); }

void ws_set_list(uint8_t rows, bool above) {
  list_rows = rows ? rows : 1;
  rescan_above = above;
}

uint8_t ws_list_top(void) {
  if (ws.sel < top) top = ws.sel;
  if (ws.sel >= top + list_rows) top = ws.sel - list_rows + 1;
  return top;
}

// ---------------- actions ----------------

bool ws_back(void) {
  switch (ws.screen) {
    case WS_LIST: case WS_SSID: case WS_RESULT: case WS_FORGET: case WS_UPD_ASK: show(WS_STATUS); break;
    case WS_PASS: show(from_list ? WS_LIST : WS_SSID); break;
    case WS_JOIN: case WS_UPD: break; // the join / the update finishes or times out
    case WS_UPD_END: esp_update_cancel(); ws_start(); break; // the new firmware may have lost the CTS
    default: return false;
  }
  return true;
}

static void open_pass(const char *name, bool list) {
  strlcpy(ws.ssid, name, sizeof(ws.ssid));
  ws.pass[0] = '\0';
  from_list = list;
  ws.show_pass = false;
  show(WS_PASS);
}

void ws_join(void) {
  show(WS_JOIN);
  run(S_JOIN);
}

void ws_scan(void) {
  show(WS_SCAN);
  run(S_SCAN);
}

void ws_next(void) {
  if (ws.ssid[0]) open_pass(ws.ssid, false);
}

void ws_toggle_pass(void) { ws.show_pass = !ws.show_pass; }

static void join_ap(uint8_t i) {
  ws.sel = i;
  if (ws.aps[i].ecn == 0) { strlcpy(ws.ssid, ws.aps[i].ssid, sizeof(ws.ssid)); ws.pass[0] = '\0'; from_list = true; ws_join(); }
  else open_pass(ws.aps[i].ssid, true);
}

void ws_click_ap(uint8_t i) {
  if (i >= ws.aps_len) return;
  ws.on_rescan = false;
  if (i == ws.sel) join_ap(i);
  else ws.sel = i;
}

void ws_wheel(int d) {
  if (ws.screen != WS_LIST || ws.aps_len == 0) return;
  ws.on_rescan = false;
  if (d > 0 && ws.sel > 0) ws.sel--;
  else if (d < 0 && ws.sel + 1 < ws.aps_len) ws.sel++;
}

void ws_menu(uint8_t i) {
  uint8_t ids[WS_M_COUNT], n = ws_menu_items(ids);
  ws.sel = i < n ? i : 0;
  uint8_t id = ids[ws.sel];
  if (id == WS_M_SCAN) ws_scan();
  else if (id == WS_M_HIDDEN) { ws.ssid[0] = '\0'; show(WS_SSID); }
  else if (id == WS_M_FORGET) { ws.screen = WS_FORGET; ws.sel = 1; } // Cancel first
  else { ws.restoring = id == WS_M_RESET; ws.screen = WS_UPD_ASK; ws.sel = 1; }
}

void ws_confirm(bool yes) {
  if (!yes) show(WS_STATUS);
  else if (ws.screen == WS_FORGET) { show(WS_INIT); run(S_UNAUTO); }
  else if (ws.screen == WS_UPD_ASK) {
    strlcpy(ws.old_version, ws.version, sizeof(ws.old_version));
    if (ws.restoring) esp_restore_start(); else esp_update_start();
    upd_percent = 0;
    upd_message = NULL;
    show(WS_UPD);
  }
}

void ws_ok(void) {
  if (ws.screen == WS_UPD_END) ws_back();
  else show(WS_STATUS);
}

// ---------------- keys ----------------

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

static void list_key(uint8_t key, bool enter) {
  // the Rescan button is one more stop past the first (above) or the last (below) network
  bool to_rescan = rescan_above ? key == KEY_UP && ws.sel == 0 : key == KEY_DOWN && ws.sel + 1 >= ws.aps_len;
  bool from_rescan = ws.on_rescan && (rescan_above ? key == KEY_DOWN : key == KEY_UP);
  if (from_rescan) ws.on_rescan = false;
  else if (to_rescan) ws.on_rescan = true;
  else if (key == KEY_UP && !ws.on_rescan && ws.sel > 0) ws.sel--;
  else if (key == KEY_DOWN && !ws.on_rescan && ws.sel + 1 < ws.aps_len) ws.sel++;
  else if (key == KEY_PAGEUP) { ws.on_rescan = false; ws.sel = ws.sel > list_rows ? ws.sel - list_rows : 0; }
  else if (key == KEY_PAGEDOWN) { ws.on_rescan = false; ws.sel = ws.sel + list_rows < ws.aps_len ? ws.sel + list_rows : (ws.aps_len ? ws.aps_len - 1 : 0); }
  else if (key == KEY_R || (enter && (ws.on_rescan || ws.aps_len == 0))) ws_scan();
  else if (enter) join_ap(ws.sel);
}

bool ws_key(uint8_t key, bool shift, bool joy) {
  bool enter = key == KEY_ENTER || key == KEY_KPENTER;
  uint8_t s = ws.screen;
  // Left goes back too where it has nothing else to do
  bool left_back = key == KEY_LEFT && !ui_repeat && (s == WS_NO_ESP || s == WS_STATUS || s == WS_LIST || s == WS_RESULT);
  if (key == KEY_ESC || left_back) {
    bool stay = ws_back();
    ws_drawn(); // the screen draws after a key
    return stay;
  }
  if (joy && (s == WS_SSID || s == WS_PASS)) {
    bool p = s == WS_PASS;
    if (ui_joy_edit(p ? ws.pass : ws.ssid, p ? WS_PASS_MAX : KG_WIFI_SSID_MAX, key)) {
      if (p) ws.show_pass = true; // the picked chars are seen
      return true;
    }
  }

  switch (s) {
    case WS_NO_ESP:
      if (enter) ws_start();
      break;
    case WS_STATUS: {
      uint8_t ids[WS_M_COUNT], items = ws_menu_items(ids);
      if (key == KEY_UP) ws.sel = ws.sel ? ws.sel - 1 : items - 1;
      else if (key == KEY_DOWN || key == KEY_TAB) ws.sel = (ws.sel + 1) % items;
      else if (enter) ws_menu(ws.sel);
      break;
    }
    case WS_FORGET:
    case WS_UPD_ASK:
      if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_TAB) ws.sel ^= 1;
      else if (enter) ws_confirm(ws.sel == 0);
      break;
    case WS_LIST:
      list_key(key, enter);
      break;
    case WS_SSID:
      if (enter) ws_next();
      else edit(ws.ssid, KG_WIFI_SSID_MAX, key, shift);
      break;
    case WS_PASS:
      if (enter) ws_join();
      else if (key == KEY_TAB) ws_toggle_pass();
      else edit(ws.pass, WS_PASS_MAX, key, shift);
      break;
    case WS_RESULT:
    case WS_UPD_END:
      if (enter) ws_ok();
      break;
  }
  ws_drawn(); // the screen draws after a key
  return true;
}
