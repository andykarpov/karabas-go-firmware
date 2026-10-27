#include <unity.h>
#include <string.h>
#include "hid_learn.h"

void setUp() {}
void tearDown() {}

static hid_learn_rest_t rest;
static hid_joy_rule_t r;

static void assert_rule(uint8_t pos, uint8_t type, uint8_t val, uint8_t arg) {
  TEST_ASSERT_EQUAL_UINT8(pos, r.pos);
  TEST_ASSERT_EQUAL_UINT8(type, r.type);
  TEST_ASSERT_EQUAL_UINT8(val, r.val);
  TEST_ASSERT_EQUAL_UINT8(arg, r.arg);
}

// DragonRise / Retrolink: axes 0x7f at rest, buttons in the high nibble of byte 5 and in byte 6
static void test_dragonrise() {
  const uint8_t idle[8] = {0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x0f, 0x00, 0x00};
  hid_learn_rest_reset(&rest);
  for (int i = 0; i < 3; i++) hid_learn_rest_add(&rest, 0, idle, 8);
  TEST_ASSERT_FALSE(hid_learn_detect(&rest, 0, idle, 8, &r));
  TEST_ASSERT_TRUE(hid_learn_is_rest(&rest, 0, idle, 8));

  uint8_t p[8];
  memcpy(p, idle, 8); p[1] = 0x00; // up
  TEST_ASSERT_TRUE(hid_learn_detect(&rest, 0, p, 8, &r));
  assert_rule(1, HID_JOY_RANGE, 0, 63);
  TEST_ASSERT_FALSE(hid_learn_is_rest(&rest, 0, p, 8));

  memcpy(p, idle, 8); p[0] = 0xff; // right: one bit moved, but the byte is centered
  TEST_ASSERT_TRUE(hid_learn_detect(&rest, 0, p, 8, &r));
  assert_rule(0, HID_JOY_RANGE, 191, 255);

  memcpy(p, idle, 8); p[5] = 0x2f; // a
  TEST_ASSERT_TRUE(hid_learn_detect(&rest, 0, p, 8, &r));
  assert_rule(5, HID_JOY_BIT, 5, 0);

  memcpy(p, idle, 8); p[6] = 0x20; // start
  TEST_ASSERT_TRUE(hid_learn_detect(&rest, 0, p, 8, &r));
  assert_rule(6, HID_JOY_BIT, 5, 0);
}

// hat in the low nibble (8 = centered), buttons in the high one, jittering sticks
static void test_hat_and_jitter() {
  hid_learn_rest_reset(&rest);
  const uint8_t a[4] = {0x08, 0x7e, 0x81, 0x00};
  const uint8_t b[4] = {0x08, 0x81, 0x7e, 0x00};
  hid_learn_rest_add(&rest, 1, a, 4);
  hid_learn_rest_add(&rest, 1, b, 4);
  TEST_ASSERT_FALSE(hid_learn_detect(&rest, 1, a, 4, &r));
  const uint8_t jitter[4] = {0x08, 0x85, 0x79, 0x00};
  TEST_ASSERT_TRUE(hid_learn_is_rest(&rest, 1, jitter, 4));
  TEST_ASSERT_FALSE(hid_learn_detect(&rest, 1, jitter, 4, &r));

  const uint8_t hat_right[4] = {0x02, 0x81, 0x7e, 0x00};
  TEST_ASSERT_TRUE(hid_learn_detect(&rest, 1, hat_right, 4, &r));
  assert_rule(0, HID_JOY_HAT, 2, 0);

  // a button in the hat byte is a bit, not the hat
  const uint8_t square[4] = {0x18, 0x81, 0x7e, 0x00};
  TEST_ASSERT_TRUE(hid_learn_detect(&rest, 1, square, 4, &r));
  assert_rule(0, HID_JOY_BIT, 4, 0);

  // a button wins over a stick moved at the same time
  const uint8_t both[4] = {0x08, 0x00, 0x7e, 0x01};
  TEST_ASSERT_TRUE(hid_learn_detect(&rest, 1, both, 4, &r));
  assert_rule(3, HID_JOY_BIT, 0, 0);

  const uint8_t stick_left[4] = {0x08, 0x10, 0x7e, 0x00};
  TEST_ASSERT_TRUE(hid_learn_detect(&rest, 1, stick_left, 4, &r));
  assert_rule(1, HID_JOY_RANGE, 0, 0x47);

  // another report id is not compared
  TEST_ASSERT_FALSE(hid_learn_detect(&rest, 2, hat_right, 4, &r));
  TEST_ASSERT_FALSE(hid_learn_is_rest(&rest, 2, a, 4));
}

static void test_hat_high_nibble() {
  hid_learn_rest_reset(&rest);
  const uint8_t idle[1] = {0xf0};
  hid_learn_rest_add(&rest, 0, idle, 1);
  const uint8_t down[1] = {0x40};
  TEST_ASSERT_TRUE(hid_learn_detect(&rest, 0, down, 1, &r));
  assert_rule(0, HID_JOY_HAT, 4, 4);
}

static void test_signed_axis() {
  hid_learn_rest_reset(&rest);
  const uint8_t a[1] = {0xff}, b[1] = {0x01}, c[1] = {0x00};
  hid_learn_rest_add(&rest, 0, a, 1);
  hid_learn_rest_add(&rest, 0, b, 1);
  hid_learn_rest_add(&rest, 0, c, 1);
  TEST_ASSERT_TRUE(hid_learn_is_rest(&rest, 0, a, 1));
  const uint8_t left[1] = {0x80};
  TEST_ASSERT_TRUE(hid_learn_detect(&rest, 0, left, 1, &r));
  assert_rule(0, HID_JOY_RANGE, 0x80, (uint8_t) -64);
  const uint8_t right[1] = {0x7f};
  TEST_ASSERT_TRUE(hid_learn_detect(&rest, 0, right, 1, &r));
  assert_rule(0, HID_JOY_RANGE, 64, 127);
}

static void test_active_low_button() {
  hid_learn_rest_reset(&rest);
  const uint8_t idle[1] = {0xff};
  hid_learn_rest_add(&rest, 0, idle, 1);
  const uint8_t p[1] = {0xfb};
  TEST_ASSERT_TRUE(hid_learn_detect(&rest, 0, p, 1, &r));
  assert_rule(0, HID_JOY_BIT_CLEAR, 2, 0);
}

static void test_detected_rule_maps_back() {
  // the learned rule must be pressed on the report it came from and released at rest
  hid_learn_rest_reset(&rest);
  const uint8_t idle[3] = {0x80, 0x0f, 0x00};
  hid_learn_rest_add(&rest, 0, idle, 3);
  const uint8_t presses[][3] = {{0x00, 0x0f, 0x00}, {0xff, 0x0f, 0x00}, {0x80, 0x06, 0x00}, {0x80, 0x0f, 0x80}};
  for (size_t i = 0; i < sizeof(presses) / sizeof(presses[0]); i++) {
    const uint8_t *p = presses[i];
    hid_joy_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.report_id = -1;
    TEST_ASSERT_TRUE(hid_learn_detect(&rest, 0, p, 3, &r));
    char buf[24];
    hid_learn_format_rule(&r, buf, sizeof(buf));
    TEST_ASSERT_TRUE(hid_joy_rules_parse(buf, 64, &cfg));
    TEST_ASSERT_EQUAL_UINT16(64, hid_joy_map_report(&cfg, p, 3, 0));
    TEST_ASSERT_EQUAL_UINT16(0, hid_joy_map_report(&cfg, idle, 3, 0));
  }
}

static void test_format() {
  char buf[24];
  hid_joy_rule_t a = {0, 5, HID_JOY_BIT, 6, 0};
  hid_learn_format_rule(&a, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("5,1,6", buf);
  hid_joy_rule_t b = {0, 1, HID_JOY_RANGE, 0, 63};
  hid_learn_format_rule(&b, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("1,3,0,63", buf);
  hid_joy_rule_t c = {0, 0, HID_JOY_HAT, 2, 0};
  hid_learn_format_rule(&c, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0,2,2", buf);
  hid_joy_rule_t d = {0, 0, HID_JOY_HAT, 2, 4};
  hid_learn_format_rule(&d, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0,2,2,4", buf);
}

// a direction pressed while the pad rests (the press that picked the pad, held or repeated)
static void test_rest_moved() {
  const uint8_t idle[8] = {0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x0f, 0x00, 0x00};
  hid_learn_rest_reset(&rest);
  hid_learn_rest_add(&rest, 0, idle, 8);
  uint8_t p[8];
  memcpy(p, idle, 8); p[1] = 0x00; // up
  TEST_ASSERT_TRUE(hid_learn_rest_moved(&rest, 0, p, 8));
  memcpy(p, idle, 8); p[1] = 0x84; // jitter
  TEST_ASSERT_FALSE(hid_learn_rest_moved(&rest, 0, p, 8));
  TEST_ASSERT_FALSE(hid_learn_rest_moved(&rest, 1, p, 8)); // another report
  // a stick that jitters around 0 is not moved by 0xff / 0x01
  const uint8_t zero[2] = {0x00, 0x00};
  hid_learn_rest_reset(&rest);
  hid_learn_rest_add(&rest, 0, zero, 2);
  const uint8_t jit[2] = {0xff, 0x01};
  TEST_ASSERT_FALSE(hid_learn_rest_moved(&rest, 0, jit, 2));
  const uint8_t push[2] = {0x80, 0x00};
  TEST_ASSERT_TRUE(hid_learn_rest_moved(&rest, 0, push, 2));
}

static void test_long_report_clamped() {
  hid_learn_rest_reset(&rest);
  uint8_t big[100];
  memset(big, 0, sizeof(big));
  hid_learn_rest_add(&rest, 0, big, sizeof(big));
  TEST_ASSERT_EQUAL_UINT16(HID_LEARN_MAX_REPORT, rest.len);
  TEST_ASSERT_TRUE(hid_learn_is_rest(&rest, 0, big, sizeof(big)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dragonrise);
  RUN_TEST(test_hat_and_jitter);
  RUN_TEST(test_hat_high_nibble);
  RUN_TEST(test_signed_axis);
  RUN_TEST(test_active_low_button);
  RUN_TEST(test_detected_rule_maps_back);
  RUN_TEST(test_format);
  RUN_TEST(test_rest_moved);
  RUN_TEST(test_long_report_clamped);
  return UNITY_END();
}
