#include <unity.h>
#include <string.h>
#include "esp_at_parse.h"
#include "hid_ascii.h"

void setUp() {}
void tearDown() {}

static void test_line() {
  TEST_ASSERT_EQUAL(KG_ESP_LINE_OK, kg_esp_line("OK"));
  TEST_ASSERT_EQUAL(KG_ESP_LINE_ERROR, kg_esp_line("ERROR"));
  TEST_ASSERT_EQUAL(KG_ESP_LINE_ERROR, kg_esp_line("FAIL"));
  TEST_ASSERT_EQUAL(KG_ESP_LINE_DATA, kg_esp_line("busy p..."));
  TEST_ASSERT_EQUAL(KG_ESP_LINE_DATA, kg_esp_line("SEND OK"));
  TEST_ASSERT_EQUAL(KG_ESP_LINE_DATA, kg_esp_line("AT+CWLAP"));
}

static void test_cwlap() {
  kg_wifi_ap_t ap;
  TEST_ASSERT_TRUE(kg_esp_parse_cwlap("+CWLAP:(3,\"Home\",-61,\"aa:bb:cc:dd:ee:ff\",6,0,0)", &ap));
  TEST_ASSERT_EQUAL_STRING("Home", ap.ssid);
  TEST_ASSERT_EQUAL(-61, ap.rssi);
  TEST_ASSERT_EQUAL(3, ap.ecn);
  // AT 2.x, only ecn ssid rssi (CWLAPOPT)
  TEST_ASSERT_TRUE(kg_esp_parse_cwlap("+CWLAP:(0,\"Cafe Free\",-80)", &ap));
  TEST_ASSERT_EQUAL_STRING("Cafe Free", ap.ssid);
  TEST_ASSERT_EQUAL(-80, ap.rssi);
  TEST_ASSERT_EQUAL(0, ap.ecn);
  // quotes and commas in the ssid
  TEST_ASSERT_TRUE(kg_esp_parse_cwlap("+CWLAP:(4,\"a\",\"b,c\",-50,\"aa:bb:cc:dd:ee:ff\",1)", &ap));
  TEST_ASSERT_EQUAL_STRING("a\",\"b,c", ap.ssid);
  TEST_ASSERT_EQUAL(-50, ap.rssi);
  // hidden
  TEST_ASSERT_TRUE(kg_esp_parse_cwlap("+CWLAP:(3,\"\",-70,\"aa:bb:cc:dd:ee:ff\",11)", &ap));
  TEST_ASSERT_EQUAL_STRING("", ap.ssid);
  TEST_ASSERT_FALSE(kg_esp_parse_cwlap("+CWJAP:\"Home\"", &ap));
  TEST_ASSERT_FALSE(kg_esp_parse_cwlap("+CWLAP:(3,Home,-61)", &ap));
}

static kg_wifi_ap_t mk(const char *ssid, int rssi) {
  kg_wifi_ap_t ap;
  strcpy(ap.ssid, ssid);
  ap.rssi = rssi;
  ap.ecn = 3;
  return ap;
}

static void test_ap_list() {
  kg_wifi_ap_t list[3];
  uint8_t n = 0;
  kg_wifi_ap_t a = mk("a", -70), b = mk("b", -50), c = mk("c", -60), d = mk("d", -90), a2 = mk("a", -40), h = mk("", -10);
  kg_wifi_ap_add(list, &n, 3, &a);
  kg_wifi_ap_add(list, &n, 3, &b);
  kg_wifi_ap_add(list, &n, 3, &c);
  TEST_ASSERT_EQUAL(3, n);
  TEST_ASSERT_EQUAL_STRING("b", list[0].ssid);
  TEST_ASSERT_EQUAL_STRING("c", list[1].ssid);
  TEST_ASSERT_EQUAL_STRING("a", list[2].ssid);
  kg_wifi_ap_add(list, &n, 3, &d); // weaker than all, list full
  TEST_ASSERT_EQUAL(3, n);
  TEST_ASSERT_EQUAL_STRING("a", list[2].ssid);
  kg_wifi_ap_add(list, &n, 3, &a2); // same ssid, stronger: moves up
  TEST_ASSERT_EQUAL(3, n);
  TEST_ASSERT_EQUAL_STRING("a", list[0].ssid);
  TEST_ASSERT_EQUAL(-40, list[0].rssi);
  TEST_ASSERT_EQUAL_STRING("b", list[1].ssid);
  TEST_ASSERT_EQUAL_STRING("c", list[2].ssid);
  kg_wifi_ap_add(list, &n, 3, &a); // same ssid, weaker: ignored
  TEST_ASSERT_EQUAL(-40, list[0].rssi);
  kg_wifi_ap_add(list, &n, 3, &h);
  TEST_ASSERT_EQUAL_STRING("a", list[0].ssid);
}

static void test_status_lines() {
  char s[33];
  TEST_ASSERT_TRUE(kg_esp_parse_cwjap("+CWJAP:\"Home\",\"aa:bb:cc:dd:ee:ff\",6,-61", s, sizeof(s)));
  TEST_ASSERT_EQUAL_STRING("Home", s);
  TEST_ASSERT_FALSE(kg_esp_parse_cwjap("+CWJAP:2", s, sizeof(s)));
  TEST_ASSERT_FALSE(kg_esp_parse_cwjap("No AP", s, sizeof(s)));
  TEST_ASSERT_EQUAL(2, kg_esp_join_error("+CWJAP:2"));
  TEST_ASSERT_EQUAL(-1, kg_esp_join_error("+CWJAP:\"Home\""));
  TEST_ASSERT_EQUAL_STRING("Wrong password", kg_esp_join_error_text(2));
  TEST_ASSERT_TRUE(kg_esp_parse_ip("+CIFSR:STAIP,\"192.168.1.5\"", s, sizeof(s)));
  TEST_ASSERT_EQUAL_STRING("192.168.1.5", s);
  TEST_ASSERT_FALSE(kg_esp_parse_ip("+CIFSR:STAMAC,\"aa:bb\"", s, sizeof(s)));
  TEST_ASSERT_EQUAL(0, kg_esp_parse_flow("+UART_DEF:115200,8,1,0,0"));
  TEST_ASSERT_EQUAL(2, kg_esp_parse_flow("+UART_DEF:115200,8,1,0,2"));
  TEST_ASSERT_EQUAL(-1, kg_esp_parse_flow("+UART_CUR:115200,8,1,0,2"));
  TEST_ASSERT_EQUAL(1, kg_esp_parse_mode("+CWMODE:1"));
  TEST_ASSERT_EQUAL(-1, kg_esp_parse_mode("+CWMODE:"));
  TEST_ASSERT_TRUE(kg_esp_parse_version("AT version:1.7.4.0(May 11 2020 19:13:04)", s, sizeof(s)));
  TEST_ASSERT_EQUAL_STRING("1.7.4.0", s);
  TEST_ASSERT_TRUE(kg_esp_parse_version("AT version:2.2.0.0(b097cdf - ESP8266 - Jun 17 2021)", s, sizeof(s)));
  TEST_ASSERT_EQUAL_STRING("2.2.0.0", s);
}

static void test_sntptime() {
  kg_datetime_t t;
  TEST_ASSERT_TRUE(kg_esp_parse_sntptime("+CIPSNTPTIME:Mon Oct 26 18:46:23 2026", &t));
  TEST_ASSERT_EQUAL(2026, t.year); TEST_ASSERT_EQUAL(10, t.month); TEST_ASSERT_EQUAL(26, t.day);
  TEST_ASSERT_EQUAL(18, t.hour); TEST_ASSERT_EQUAL(46, t.minute); TEST_ASSERT_EQUAL(23, t.second);
  // AT 1.x pads the day with a space
  TEST_ASSERT_TRUE(kg_esp_parse_sntptime("+CIPSNTPTIME:Thu Aug  4 04:08:05 2016", &t));
  TEST_ASSERT_EQUAL(2016, t.year); TEST_ASSERT_EQUAL(8, t.month); TEST_ASSERT_EQUAL(4, t.day);
  TEST_ASSERT_EQUAL(4, t.hour); TEST_ASSERT_EQUAL(8, t.minute); TEST_ASSERT_EQUAL(5, t.second);
  TEST_ASSERT_TRUE(kg_esp_parse_sntptime("+CIPSNTPTIME:Sat Jan 01 00:00:00 2000", &t));
  TEST_ASSERT_EQUAL(1, t.month);
  TEST_ASSERT_TRUE(kg_esp_parse_sntptime("+CIPSNTPTIME:Fri Dec 31 23:59:59 2099", &t));
  TEST_ASSERT_EQUAL(12, t.month);
  // not synced yet
  TEST_ASSERT_FALSE(kg_esp_parse_sntptime("+CIPSNTPTIME:Thu Jan 01 00:00:03 1970", &t));
  TEST_ASSERT_FALSE(kg_esp_parse_sntptime("+CIPSNTPTIME:Mon Foo 26 18:46:23 2026", &t));
  TEST_ASSERT_FALSE(kg_esp_parse_sntptime("+CIPSNTPTIME:Mon ctN 26 18:46:23 2026", &t));
  TEST_ASSERT_FALSE(kg_esp_parse_sntptime("+CIPSNTPTIME:Mon Oct 26 24:46:23 2026", &t));
  TEST_ASSERT_FALSE(kg_esp_parse_sntptime("+CIPSNTPTIME:Mon Oct 26 18:46", &t));
  TEST_ASSERT_FALSE(kg_esp_parse_sntptime("AT+CIPSNTPTIME?", &t));
  TEST_ASSERT_FALSE(kg_esp_parse_sntptime("+TIME_UPDATED", &t));
}

static void test_quote() {
  char out[32];
  kg_esp_quote("pa\"ss,w\\d", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("pa\\\"ss\\,w\\\\d", out);
  kg_esp_quote("abc,", out, 5); // the escape does not fit: cut before it
  TEST_ASSERT_EQUAL_STRING("abc", out);
}

static void test_ciupdate() {
  int st = 0;
  TEST_ASSERT_TRUE(kg_esp_parse_ciupdate("+CIPUPDATE:1", &st));
  TEST_ASSERT_EQUAL(1, st);
  TEST_ASSERT_TRUE(kg_esp_parse_ciupdate("+CIPUPDATE:4", &st));
  TEST_ASSERT_EQUAL(4, st);
  TEST_ASSERT_TRUE(kg_esp_parse_ciupdate("+CIPUPDATE:-1", &st));
  TEST_ASSERT_EQUAL(-1, st);
  TEST_ASSERT_FALSE(kg_esp_parse_ciupdate("+CIPUPDATE:", &st));
  TEST_ASSERT_FALSE(kg_esp_parse_ciupdate("AT+CIUPDATE", &st));
  TEST_ASSERT_FALSE(kg_esp_parse_ciupdate("OK", &st));
}

static void test_update_progress() {
  TEST_ASSERT_EQUAL(0, kg_esp_update_progress(KG_ESP_UPD_SENT, 0));
  TEST_ASSERT_EQUAL(5, kg_esp_update_progress(KG_ESP_UPD_SENT, 5000));
  TEST_ASSERT_EQUAL(10, kg_esp_update_progress(KG_ESP_UPD_SENT, 60000));
  TEST_ASSERT_EQUAL(12, kg_esp_update_progress(1, 0));
  TEST_ASSERT_EQUAL(20, kg_esp_update_progress(3, 100));
  TEST_ASSERT_EQUAL(20, kg_esp_update_progress(KG_ESP_UPD_DOWNLOAD, 0));
  TEST_ASSERT_EQUAL(52, kg_esp_update_progress(KG_ESP_UPD_DOWNLOAD, 10000));
  TEST_ASSERT_EQUAL(85, kg_esp_update_progress(KG_ESP_UPD_DOWNLOAD, 20000));
  TEST_ASSERT_EQUAL(87, kg_esp_update_progress(KG_ESP_UPD_DOWNLOAD, 40000));
  TEST_ASSERT_EQUAL(89, kg_esp_update_progress(KG_ESP_UPD_DOWNLOAD, 300000));
  // it only grows while downloading
  uint8_t prev = 0;
  for (uint32_t ms = 0; ms <= 300000; ms += 500) {
    uint8_t p = kg_esp_update_progress(KG_ESP_UPD_DOWNLOAD, ms);
    TEST_ASSERT_TRUE(p >= prev && p < 90);
    prev = p;
  }
  TEST_ASSERT_EQUAL(90, kg_esp_update_progress(KG_ESP_UPD_RESTART, 0));
  TEST_ASSERT_EQUAL(98, kg_esp_update_progress(KG_ESP_UPD_RESTART, 30000));
  TEST_ASSERT_EQUAL(100, kg_esp_update_progress(KG_ESP_UPD_DONE, 0));
}

static void test_hid_ascii() {
  TEST_ASSERT_EQUAL('a', kg_hid_ascii(0x04, false));
  TEST_ASSERT_EQUAL('Z', kg_hid_ascii(0x1d, true));
  TEST_ASSERT_EQUAL('1', kg_hid_ascii(0x1e, false));
  TEST_ASSERT_EQUAL('!', kg_hid_ascii(0x1e, true));
  TEST_ASSERT_EQUAL('0', kg_hid_ascii(0x27, false));
  TEST_ASSERT_EQUAL(')', kg_hid_ascii(0x27, true));
  TEST_ASSERT_EQUAL(0, kg_hid_ascii(0x28, false)); // Enter
  TEST_ASSERT_EQUAL(0, kg_hid_ascii(0x2a, false)); // Backspace
  TEST_ASSERT_EQUAL(' ', kg_hid_ascii(0x2c, false));
  TEST_ASSERT_EQUAL('_', kg_hid_ascii(0x2d, true));
  TEST_ASSERT_EQUAL('"', kg_hid_ascii(0x34, true));
  TEST_ASSERT_EQUAL('?', kg_hid_ascii(0x38, true));
  TEST_ASSERT_EQUAL('7', kg_hid_ascii(0x5f, false));
  TEST_ASSERT_EQUAL('0', kg_hid_ascii(0x62, false));
  TEST_ASSERT_EQUAL(0, kg_hid_ascii(0x4f, false)); // Right
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_line);
  RUN_TEST(test_cwlap);
  RUN_TEST(test_ap_list);
  RUN_TEST(test_status_lines);
  RUN_TEST(test_sntptime);
  RUN_TEST(test_quote);
  RUN_TEST(test_ciupdate);
  RUN_TEST(test_update_progress);
  RUN_TEST(test_hid_ascii);
  return UNITY_END();
}
