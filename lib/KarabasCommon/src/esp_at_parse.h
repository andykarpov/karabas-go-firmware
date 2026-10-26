#pragma once

#include <stddef.h>
#include <stdint.h>
#include "time_zone.h"

// Parsing of the ESP8266 AT firmware replies (AT 1.x and 2.x), one line at a time without the
// CR LF, and quoting of the command arguments.

typedef enum {
  KG_ESP_LINE_DATA,  // anything else: a reply line, an echo, "busy p...", "WIFI GOT IP"
  KG_ESP_LINE_OK,
  KG_ESP_LINE_ERROR, // ERROR or FAIL
} kg_esp_line_t;

kg_esp_line_t kg_esp_line(const char *line);

#define KG_WIFI_SSID_MAX 32

typedef struct {
  char ssid[KG_WIFI_SSID_MAX + 1];
  int8_t rssi;
  uint8_t ecn; // 0: open
} kg_wifi_ap_t;

// +CWLAP:(3,"ssid",-60,"aa:bb:cc:dd:ee:ff",6,...) -> true
bool kg_esp_parse_cwlap(const char *line, kg_wifi_ap_t *ap);
// adds ap to list (count items, at most max), sorted by rssi, strongest first. A ssid that is
// already there keeps the stronger one; hidden networks (empty ssid) are skipped.
void kg_wifi_ap_add(kg_wifi_ap_t *list, uint8_t *count, uint8_t max, const kg_wifi_ap_t *ap);

// +CWJAP:"ssid","mac",ch,rssi... -> the ssid of the joined network
bool kg_esp_parse_cwjap(const char *line, char *ssid, size_t size);
// +CWJAP:2 (the reply of a failed join) -> 2, -1 for other lines
int kg_esp_join_error(const char *line);
// the text of a join error code
const char *kg_esp_join_error_text(int code);
// +CIFSR:STAIP,"192.168.1.5" -> the address
bool kg_esp_parse_ip(const char *line, char *ip, size_t size);
// +UART_DEF:115200,8,1,0,2 -> the flow control (the last number), -1 for other lines
int kg_esp_parse_flow(const char *line);
// +CWMODE:1 -> 1, -1 for other lines
int kg_esp_parse_mode(const char *line);
// "AT version:1.7.4.0(May 11 2020 19:13:04)" -> "1.7.4.0"
bool kg_esp_parse_version(const char *line, char *version, size_t size);

// +CIPSNTPTIME:Mon Oct 26 18:46:23 2026 (UTC with the timezone 0 of AT+CIPSNTPCFG) -> true.
// False for other lines and for a year out of 2000..2099: before the first sync the ESP gives
// 1970.
bool kg_esp_parse_sntptime(const char *line, kg_datetime_t *t);

// s quoted for an AT string argument: '"', ',' and '\' get a '\' before them. out must hold
// 2 * strlen(s) + 1 bytes to never cut it.
void kg_esp_quote(const char *s, char *out, size_t size);
