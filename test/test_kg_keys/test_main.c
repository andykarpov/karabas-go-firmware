#include "unity.h"
#include "kg_keys.h"

void setUp(void) {}
void tearDown(void) {}

static void check(const char *s, uint8_t mod, uint8_t key) {
  uint8_t m = 0xff, k = 0xff;
  TEST_ASSERT_TRUE_MESSAGE(kg_key_parse(s, &m, &k), s);
  TEST_ASSERT_EQUAL_HEX8_MESSAGE(mod, m, s);
  TEST_ASSERT_EQUAL_HEX8_MESSAGE(key, k, s);
}

static void bad(const char *s) {
  uint8_t m, k;
  TEST_ASSERT_FALSE_MESSAGE(kg_key_parse(s, &m, &k), s);
}

static void test_parse() {
  check("a", 0x00, 0x04);
  check("Z", 0x00, 0x1d);
  check("1", 0x00, 0x1e);
  check("0", 0x00, 0x27);
  check("enter", 0x00, 0x28);
  check("ESC", 0x00, 0x29);
  check("f1", 0x00, 0x3a);
  check("F12", 0x00, 0x45);
  check("ctrl+alt+del", 0x05, 0x4c);
  check("menu+esc", 0x08, 0x29);
  check("menu+F3", 0x08, 0x3c);
  check("shift+tab", 0x02, 0x2b);
  check("alt", 0x04, 0x00);
  check("0x29", 0x00, 0x29);
  check("ralt+up", 0x40, 0x52);
}

static void test_bad() {
  bad("");
  bad("foo");
  bad("a+b");
  bad("ctrl+");
  bad("+a");
  bad("f13");
  bad("f0");
  bad("0x");
  bad("0x00");
  bad("0xzz");
}

static void test_ascii() {
  uint8_t m, k;
  TEST_ASSERT_TRUE(kg_key_ascii('a', &m, &k)); TEST_ASSERT_EQUAL_HEX8(0, m); TEST_ASSERT_EQUAL_HEX8(0x04, k);
  TEST_ASSERT_TRUE(kg_key_ascii('A', &m, &k)); TEST_ASSERT_EQUAL_HEX8(2, m); TEST_ASSERT_EQUAL_HEX8(0x04, k);
  TEST_ASSERT_TRUE(kg_key_ascii(':', &m, &k)); TEST_ASSERT_EQUAL_HEX8(2, m); TEST_ASSERT_EQUAL_HEX8(0x33, k);
  TEST_ASSERT_TRUE(kg_key_ascii(' ', &m, &k)); TEST_ASSERT_EQUAL_HEX8(0, m); TEST_ASSERT_EQUAL_HEX8(0x2c, k);
  TEST_ASSERT_TRUE(kg_key_ascii('\\', &m, &k)); TEST_ASSERT_EQUAL_HEX8(0, m); TEST_ASSERT_EQUAL_HEX8(0x31, k);
  TEST_ASSERT_TRUE(kg_key_ascii('0', &m, &k)); TEST_ASSERT_EQUAL_HEX8(0, m); TEST_ASSERT_EQUAL_HEX8(0x27, k);
  TEST_ASSERT_TRUE(kg_key_ascii('?', &m, &k)); TEST_ASSERT_EQUAL_HEX8(2, m); TEST_ASSERT_EQUAL_HEX8(0x38, k);
  TEST_ASSERT_FALSE(kg_key_ascii('\t', &m, &k));
  TEST_ASSERT_FALSE(kg_key_ascii(0, &m, &k));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parse);
  RUN_TEST(test_bad);
  RUN_TEST(test_ascii);
  return UNITY_END();
}
