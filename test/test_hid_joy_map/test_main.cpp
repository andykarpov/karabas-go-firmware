#include <unity.h>
#include <string.h>
#include "hid_joy_map.h"

void setUp() {}
void tearDown() {}

static hid_joy_button_config_t btn() {
  hid_joy_button_config_t b;
  memset(&b, 0, sizeof(b));
  return b;
}

static void test_parse_bit() {
  hid_joy_button_config_t b = btn();
  TEST_ASSERT_TRUE(hid_joy_button_parse("5,1,6", &b));
  TEST_ASSERT_TRUE(b.isAvail);
  TEST_ASSERT_TRUE(b.isBit);
  TEST_ASSERT_EQUAL_UINT8(5, b.pos);
  TEST_ASSERT_EQUAL_UINT8(6, b.val);
}

static void test_parse_value() {
  hid_joy_button_config_t b = btn();
  TEST_ASSERT_TRUE(hid_joy_button_parse("1,0,255", &b));
  TEST_ASSERT_FALSE(b.isBit);
  TEST_ASSERT_EQUAL_UINT8(1, b.pos);
  TEST_ASSERT_EQUAL_UINT8(255, b.val);
}

static void test_parse_spaces_and_defaults() {
  hid_joy_button_config_t b = btn();
  TEST_ASSERT_TRUE(hid_joy_button_parse(" 2 , 1 , 3 ", &b));
  TEST_ASSERT_EQUAL_UINT8(2, b.pos);
  TEST_ASSERT_EQUAL_UINT8(3, b.val);
  b = btn();
  TEST_ASSERT_TRUE(hid_joy_button_parse("4", &b));
  TEST_ASSERT_EQUAL_UINT8(4, b.pos);
  TEST_ASSERT_FALSE(b.isBit);
  TEST_ASSERT_EQUAL_UINT8(0, b.val);
}

static void test_parse_invalid() {
  hid_joy_button_config_t b = btn();
  TEST_ASSERT_FALSE(hid_joy_button_parse("", &b));
  TEST_ASSERT_FALSE(hid_joy_button_parse("a,b,c", &b));
  TEST_ASSERT_FALSE(hid_joy_button_parse("1,2,3,4", &b));   // too many fields (overflowed strings[4] before)
  TEST_ASSERT_FALSE(hid_joy_button_parse("1,1,9", &b));     // bit number > 7
  TEST_ASSERT_FALSE(hid_joy_button_parse("300,0,0", &b));   // position > 255
  TEST_ASSERT_FALSE(b.isAvail);
}

static void test_map_report() {
  hid_joy_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  // up: byte 1 == 0, a: byte 5 bit 5
  cfg.buttons[0] = btn(); hid_joy_button_parse("1,0,0", &cfg.buttons[0]); cfg.buttons[0].btn = 2;
  cfg.buttons[1] = btn(); hid_joy_button_parse("5,1,5", &cfg.buttons[1]); cfg.buttons[1].btn = 64;

  uint8_t idle[8] = {0x7f, 0x7f, 0, 0, 0, 0x0f, 0, 0};
  TEST_ASSERT_EQUAL_UINT16(1, hid_joy_map_report(&cfg, idle, sizeof(idle), 1));

  uint8_t pressed[8] = {0x7f, 0x00, 0, 0, 0, 0x2f, 0, 0};
  TEST_ASSERT_EQUAL_UINT16(1 | 2 | 64, hid_joy_map_report(&cfg, pressed, sizeof(pressed), 1));
}

static void test_map_report_short_report() {
  hid_joy_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  hid_joy_button_parse("5,1,0", &cfg.buttons[0]); cfg.buttons[0].btn = 64;
  uint8_t report[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  // byte 5 is outside a 3-byte report and must not be read
  TEST_ASSERT_EQUAL_UINT16(0, hid_joy_map_report(&cfg, report, 3, 0));
}

static void test_map_report_duplicate_button() {
  hid_joy_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  hid_joy_button_parse("0,1,0", &cfg.buttons[0]); cfg.buttons[0].btn = 64;
  hid_joy_button_parse("0,1,1", &cfg.buttons[1]); cfg.buttons[1].btn = 64;
  uint8_t report[1] = {0x03};
  // two inputs mapped to the same sega button must not carry into the next bit
  TEST_ASSERT_EQUAL_UINT16(64, hid_joy_map_report(&cfg, report, 1, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_bit);
  RUN_TEST(test_parse_value);
  RUN_TEST(test_parse_spaces_and_defaults);
  RUN_TEST(test_parse_invalid);
  RUN_TEST(test_map_report);
  RUN_TEST(test_map_report_short_report);
  RUN_TEST(test_map_report_duplicate_button);
  return UNITY_END();
}
