#include <unity.h>
#include <string.h>
#include "hid_joy_map.h"

void setUp() {}
void tearDown() {}

static hid_joy_config_t cfg;

static void reset() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.report_id = -1;
}

static void test_parse_bit() {
  reset();
  TEST_ASSERT_TRUE(hid_joy_rules_parse("5,1,6", 64, &cfg));
  TEST_ASSERT_EQUAL_UINT8(1, cfg.count);
  TEST_ASSERT_EQUAL_UINT8(HID_JOY_BIT, cfg.rules[0].type);
  TEST_ASSERT_EQUAL_UINT8(5, cfg.rules[0].pos);
  TEST_ASSERT_EQUAL_UINT8(6, cfg.rules[0].val);
  TEST_ASSERT_EQUAL_UINT16(64, cfg.rules[0].btn);
}

static void test_parse_value() {
  reset();
  TEST_ASSERT_TRUE(hid_joy_rules_parse("1,0,255", 2, &cfg));
  TEST_ASSERT_EQUAL_UINT8(HID_JOY_VALUE, cfg.rules[0].type);
  TEST_ASSERT_EQUAL_UINT8(1, cfg.rules[0].pos);
  TEST_ASSERT_EQUAL_UINT8(255, cfg.rules[0].val);
}

static void test_parse_spaces_and_defaults() {
  reset();
  TEST_ASSERT_TRUE(hid_joy_rules_parse(" 2 , 1 , 3 ", 2, &cfg));
  TEST_ASSERT_EQUAL_UINT8(2, cfg.rules[0].pos);
  TEST_ASSERT_EQUAL_UINT8(3, cfg.rules[0].val);
  TEST_ASSERT_TRUE(hid_joy_rules_parse("4", 2, &cfg));
  TEST_ASSERT_EQUAL_UINT8(4, cfg.rules[1].pos);
  TEST_ASSERT_EQUAL_UINT8(HID_JOY_VALUE, cfg.rules[1].type);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.rules[1].val);
  TEST_ASSERT_TRUE(hid_joy_rules_parse("1 , 3 , 0 , 63 | 5 , 2 , 0", 2, &cfg));
  TEST_ASSERT_EQUAL_UINT8(4, cfg.count);
}

static void test_parse_invalid() {
  reset();
  TEST_ASSERT_FALSE(hid_joy_rules_parse("", 2, &cfg));
  TEST_ASSERT_FALSE(hid_joy_rules_parse("a,b,c", 2, &cfg));
  TEST_ASSERT_FALSE(hid_joy_rules_parse("1,0,3,4", 2, &cfg));     // no arg for a value
  TEST_ASSERT_FALSE(hid_joy_rules_parse("1,1,2,3,4", 2, &cfg));   // too many fields
  TEST_ASSERT_FALSE(hid_joy_rules_parse("1,1,9", 2, &cfg));       // bit number > 7
  TEST_ASSERT_FALSE(hid_joy_rules_parse("1,4,8", 2, &cfg));
  TEST_ASSERT_FALSE(hid_joy_rules_parse("300,0,0", 2, &cfg));     // position > 255
  TEST_ASSERT_FALSE(hid_joy_rules_parse("1,2,8", 2, &cfg));       // hat direction > 7
  TEST_ASSERT_FALSE(hid_joy_rules_parse("1,2,0,5", 2, &cfg));     // hat shift > 4
  TEST_ASSERT_FALSE(hid_joy_rules_parse("1,3,10", 2, &cfg));      // range without its end
  TEST_ASSERT_FALSE(hid_joy_rules_parse("1,3,10,5", 2, &cfg));    // empty range
  TEST_ASSERT_FALSE(hid_joy_rules_parse("1,5,0", 2, &cfg));       // unknown type
  TEST_ASSERT_FALSE(hid_joy_rules_parse("1,0,0|", 2, &cfg));      // empty source
  TEST_ASSERT_FALSE(hid_joy_rules_parse("1,0,0|x", 2, &cfg));
  TEST_ASSERT_EQUAL_UINT8(0, cfg.count); // nothing appended by a failed parse
}

static void test_parse_full() {
  reset();
  for (uint8_t i = 0; i < HID_JOY_MAX_RULES; i++) TEST_ASSERT_TRUE(hid_joy_rules_parse("0,1,0", 2, &cfg));
  TEST_ASSERT_FALSE(hid_joy_rules_parse("0,1,0", 2, &cfg));
  TEST_ASSERT_EQUAL_UINT8(HID_JOY_MAX_RULES, cfg.count);
}

static void test_map_report() {
  reset();
  // up: byte 1 == 0, a: byte 5 bit 5
  hid_joy_rules_parse("1,0,0", 2, &cfg);
  hid_joy_rules_parse("5,1,5", 64, &cfg);

  uint8_t idle[8] = {0x7f, 0x7f, 0, 0, 0, 0x0f, 0, 0};
  TEST_ASSERT_EQUAL_UINT16(1, hid_joy_map_report(&cfg, idle, sizeof(idle), 1));

  uint8_t pressed[8] = {0x7f, 0x00, 0, 0, 0, 0x2f, 0, 0};
  TEST_ASSERT_EQUAL_UINT16(1 | 2 | 64, hid_joy_map_report(&cfg, pressed, sizeof(pressed), 1));
}

static void test_map_report_short_report() {
  reset();
  hid_joy_rules_parse("5,1,0", 64, &cfg);
  uint8_t report[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  // byte 5 is outside a 3-byte report and must not be read
  TEST_ASSERT_EQUAL_UINT16(0, hid_joy_map_report(&cfg, report, 3, 0));
}

static void test_map_report_duplicate_button() {
  reset();
  hid_joy_rules_parse("0,1,0", 64, &cfg);
  hid_joy_rules_parse("0,1,1", 64, &cfg);
  uint8_t report[1] = {0x03};
  // two inputs mapped to the same sega button must not carry into the next bit
  TEST_ASSERT_EQUAL_UINT16(64, hid_joy_map_report(&cfg, report, 1, 0));
}

static void test_map_hat() {
  reset();
  // hat in the high nibble of byte 0, buttons in the low one
  hid_joy_rules_parse("0,2,0,4", 2, &cfg);  // up
  hid_joy_rules_parse("0,2,2,4", 16, &cfg); // right
  hid_joy_rules_parse("0,2,4,4", 4, &cfg);  // down
  hid_joy_rules_parse("0,2,6,4", 8, &cfg);  // left
  const uint16_t dirs[8] = {2, 2 | 16, 16, 4 | 16, 4, 4 | 8, 8, 2 | 8};
  for (uint8_t i = 0; i < 8; i++) {
    uint8_t r[1] = {(uint8_t) ((i << 4) | 0x0f)};
    TEST_ASSERT_EQUAL_UINT16(dirs[i], hid_joy_map_report(&cfg, r, 1, 0));
  }
  uint8_t centered[2][1] = {{0x8f}, {0xff}};
  TEST_ASSERT_EQUAL_UINT16(0, hid_joy_map_report(&cfg, centered[0], 1, 0));
  TEST_ASSERT_EQUAL_UINT16(0, hid_joy_map_report(&cfg, centered[1], 1, 0));
}

static void test_map_range_and_sources() {
  reset();
  // left: stick below a quarter, or the hat (low nibble of byte 2) pointing left
  hid_joy_rules_parse("0,3,0,63|2,2,6", 8, &cfg);
  hid_joy_rules_parse("0,3,192,255", 16, &cfg);
  uint8_t r[3] = {0x80, 0, 0x0f};
  TEST_ASSERT_EQUAL_UINT16(0, hid_joy_map_report(&cfg, r, 3, 0));
  r[0] = 63;
  TEST_ASSERT_EQUAL_UINT16(8, hid_joy_map_report(&cfg, r, 3, 0));
  r[0] = 64;
  TEST_ASSERT_EQUAL_UINT16(0, hid_joy_map_report(&cfg, r, 3, 0));
  r[0] = 192;
  TEST_ASSERT_EQUAL_UINT16(16, hid_joy_map_report(&cfg, r, 3, 0));
  r[0] = 0x80; r[2] = 0x06;
  TEST_ASSERT_EQUAL_UINT16(8, hid_joy_map_report(&cfg, r, 3, 0));
}

static void test_map_bit_clear() {
  reset();
  hid_joy_rules_parse("0,4,3", 32, &cfg);
  uint8_t r[1] = {0xff};
  TEST_ASSERT_EQUAL_UINT16(0, hid_joy_map_report(&cfg, r, 1, 0));
  r[0] = 0xf7;
  TEST_ASSERT_EQUAL_UINT16(32, hid_joy_map_report(&cfg, r, 1, 0));
}

static void test_report_id() {
  reset();
  TEST_ASSERT_TRUE(hid_joy_accepts_report(&cfg, 0));
  TEST_ASSERT_TRUE(hid_joy_accepts_report(&cfg, 3));
  cfg.report_id = 1;
  TEST_ASSERT_TRUE(hid_joy_accepts_report(&cfg, 1));
  TEST_ASSERT_FALSE(hid_joy_accepts_report(&cfg, 0));
  TEST_ASSERT_FALSE(hid_joy_accepts_report(&cfg, 2));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_bit);
  RUN_TEST(test_parse_value);
  RUN_TEST(test_parse_spaces_and_defaults);
  RUN_TEST(test_parse_invalid);
  RUN_TEST(test_parse_full);
  RUN_TEST(test_map_report);
  RUN_TEST(test_map_report_short_report);
  RUN_TEST(test_map_report_duplicate_button);
  RUN_TEST(test_map_hat);
  RUN_TEST(test_map_range_and_sources);
  RUN_TEST(test_map_bit_clear);
  RUN_TEST(test_report_id);
  return UNITY_END();
}
