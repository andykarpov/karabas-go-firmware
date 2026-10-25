#include "esp_at_parse.h"
#include <string.h>
#include <stdlib.h>

static bool starts_with(const char *s, const char *prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

static bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

// copies at most n chars of s to out (size bytes)
static void copy(char *out, size_t size, const char *s, size_t n) {
  if (size == 0) return;
  if (n > size - 1) n = size - 1;
  memcpy(out, s, n);
  out[n] = '\0';
}

kg_esp_line_t kg_esp_line(const char *line) {
  if (strcmp(line, "OK") == 0) return KG_ESP_LINE_OK;
  if (strcmp(line, "ERROR") == 0 || strcmp(line, "FAIL") == 0) return KG_ESP_LINE_ERROR;
  return KG_ESP_LINE_DATA;
}

// the end of a quoted string that starts at s (after its '"'): the first '"' followed by ','
// and the next field (a number, or a string if next_string), or by the end of the line / ')'.
// A ssid may hold '"' and ','.
static const char *string_end(const char *s, bool next_string) {
  for (const char *p = s; *p; p++) {
    if (*p != '"') continue;
    const char *n = p + 1;
    if (*n == '\0' || *n == ')') return p;
    if (*n != ',') continue;
    n++;
    if (!next_string && *n == '-') n++;
    if (next_string ? *n == '"' : is_digit(*n)) return p;
  }
  return NULL;
}

bool kg_esp_parse_cwlap(const char *line, kg_wifi_ap_t *ap) {
  if (!starts_with(line, "+CWLAP:(")) return false;
  const char *p = line + 8;
  if (!is_digit(*p)) return false;
  int ecn = atoi(p);
  while (is_digit(*p)) p++;
  if (p[0] != ',' || p[1] != '"') return false;
  const char *ssid = p + 2;
  const char *end = string_end(ssid, false);
  if (!end || end[1] != ',') return false;
  copy(ap->ssid, sizeof(ap->ssid), ssid, end - ssid);
  ap->ecn = (uint8_t) ecn;
  ap->rssi = (int8_t) atoi(end + 2);
  return true;
}

void kg_wifi_ap_add(kg_wifi_ap_t *list, uint8_t *count, uint8_t max, const kg_wifi_ap_t *ap) {
  if (ap->ssid[0] == '\0') return;
  for (uint8_t i = 0; i < *count; i++) {
    if (strcmp(list[i].ssid, ap->ssid) != 0) continue;
    if (ap->rssi <= list[i].rssi) return;
    // remove the weaker one, then insert as a new one
    memmove(&list[i], &list[i + 1], (*count - i - 1) * sizeof(*list));
    (*count)--;
    break;
  }
  uint8_t pos = 0;
  while (pos < *count && list[pos].rssi >= ap->rssi) pos++;
  if (pos >= max) return;
  uint8_t n = *count < max ? *count : max - 1;
  memmove(&list[pos + 1], &list[pos], (n - pos) * sizeof(*list));
  list[pos] = *ap;
  if (*count < max) (*count)++;
}

bool kg_esp_parse_cwjap(const char *line, char *ssid, size_t size) {
  if (!starts_with(line, "+CWJAP:\"")) return false;
  const char *s = line + 8;
  const char *end = string_end(s, true);
  if (!end) return false;
  copy(ssid, size, s, end - s);
  return true;
}

int kg_esp_join_error(const char *line) {
  if (!starts_with(line, "+CWJAP:") || !is_digit(line[7])) return -1;
  return atoi(line + 7);
}

const char *kg_esp_join_error_text(int code) {
  switch (code) {
    case 1: return "Connection timeout";
    case 2: return "Wrong password";
    case 3: return "Network not found";
    default: return "Connection failed";
  }
}

bool kg_esp_parse_ip(const char *line, char *ip, size_t size) {
  if (!starts_with(line, "+CIFSR:STAIP,\"")) return false;
  const char *s = line + 14;
  const char *end = strchr(s, '"');
  if (!end) return false;
  copy(ip, size, s, end - s);
  return true;
}

int kg_esp_parse_flow(const char *line) {
  if (!starts_with(line, "+UART_DEF:")) return -1;
  const char *last = strrchr(line, ',');
  if (!last || !is_digit(last[1])) return -1;
  return atoi(last + 1);
}

int kg_esp_parse_mode(const char *line) {
  if (!starts_with(line, "+CWMODE:") || !is_digit(line[8])) return -1;
  return atoi(line + 8);
}

bool kg_esp_parse_version(const char *line, char *version, size_t size) {
  if (!starts_with(line, "AT version:")) return false;
  const char *s = line + 11;
  size_t n = 0;
  while (s[n] && s[n] != '(' && s[n] != ' ') n++;
  copy(version, size, s, n);
  return true;
}

void kg_esp_quote(const char *s, char *out, size_t size) {
  if (size == 0) return;
  size_t o = 0;
  for (; *s; s++) {
    bool esc = *s == '"' || *s == ',' || *s == '\\';
    if (o + (esc ? 2 : 1) > size - 1) break;
    if (esc) out[o++] = '\\';
    out[o++] = *s;
  }
  out[o] = '\0';
}
