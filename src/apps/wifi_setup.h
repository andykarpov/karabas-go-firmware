#pragma once

// The setup of the ESP8266 shared by Setup Wi-Fi (app_wifi, text) and the Wi-Fi page of the
// graphic setup (app_ft_wifi), BOOT core only (it bridges the ESP UART): the module status, a
// scan, the password of the chosen network, the join. The ESP keeps the network in its flash and
// joins it on start (CWAUTOCONN); Forget turns that off and leaves the network. The status steps
// turn on the hardware flow control of the ESP: the cores stop it with CTS when their receive
// fifo is nearly full. Update module (when joined): the AT firmware from Espressif's server
// (periph/esp_update); Reset module: AT+RESTORE, for modules whose saved settings got broken.
// The status is asked again after both. The screens only lay it out and map their keys / clicks
// onto it.

#include <stdbool.h>
#include <stdint.h>
#include "esp_at_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WS_AP_MAX 32
#define WS_PASS_MAX 64

// the screens
enum { WS_INIT, WS_NO_ESP, WS_STATUS, WS_SCAN, WS_LIST, WS_SSID, WS_PASS, WS_JOIN, WS_RESULT, WS_FORGET, WS_UPD_ASK, WS_UPD, WS_UPD_END };
enum { WS_CTS_UNKNOWN, WS_CTS_ON, WS_CTS_OFF };
// the buttons of the status screen; Forget and Update only when joined
enum { WS_M_SCAN, WS_M_HIDDEN, WS_M_FORGET, WS_M_UPDATE, WS_M_RESET, WS_M_COUNT };
extern const char *const ws_menu_names[WS_M_COUNT];

typedef struct {
  uint8_t screen;
  uint8_t sel;        // the focus: the menu item, the network, the button of a question
  bool on_rescan;     // the list: the Rescan button has the focus
  bool restoring;     // the reset screens, not the update ones
  char version[16];
  char old_version[16]; // before the update
  uint8_t cts;
  char cur_ssid[KG_WIFI_SSID_MAX + 1]; // the network joined
  char ip[16];
  kg_wifi_ap_t aps[WS_AP_MAX];
  uint8_t aps_len;
  char ssid[KG_WIFI_SSID_MAX + 1];     // the network to join
  char pass[WS_PASS_MAX + 1];
  bool show_pass;
  const char *result_error;            // WS_RESULT: NULL when joined
} ws_t;

const ws_t *ws_state(void);
// the status asked again
void ws_start(void);
// no more AT steps (a running update goes on by itself)
void ws_stop(void);
// the AT steps, the update progress; true if the screen should redraw
bool ws_tick(void);
// the screen drew what changed (after a click): no redraw from the next tick
void ws_drawn(void);
// the list on the screen: rows of it, the Rescan button above it (else below)
void ws_set_list(uint8_t rows, bool rescan_above);
// the first network on the screen for the focus
uint8_t ws_list_top(void);
// the menu items of the status screen (WS_M_*), their count
uint8_t ws_menu_items(uint8_t *ids);

// a key (joy: from the joystick, it picks the chars of the text fields); false when Esc left
// the first screen
bool ws_key(uint8_t key, bool shift, bool joy);
// the screen before; false on the first one
bool ws_back(void);

// the clicks of the graphic page
void ws_menu(uint8_t i);         // the menu item i of the status screen
void ws_click_ap(uint8_t i);     // the focus, or the join of the focused network
void ws_wheel(int d);            // the list one up (d > 0) / down
void ws_scan(void);
void ws_next(void);              // the network name typed: its password
void ws_join(void);
void ws_toggle_pass(void);
void ws_confirm(bool yes);       // the answer of the Forget / Update / Reset question
void ws_ok(void);                // the OK of a result

#ifdef __cplusplus
}
#endif
