#include <unity.h>
#include <string.h>
#include "at_args.h"
#include "hid_auto_map.h"

void setUp() {}
void tearDown() {}

static void test_mouse_parse() {
  kg_at_mouse_t m;
  TEST_ASSERT_TRUE(kg_at_mouse_parse("5,-7", &m));
  TEST_ASSERT_EQUAL_INT(5, m.dx);
  TEST_ASSERT_EQUAL_INT(-7, m.dy);
  TEST_ASSERT_EQUAL_INT(KG_AT_MOUSE_KEEP, m.buttons);
  TEST_ASSERT_EQUAL_INT(0, m.wheel);
  TEST_ASSERT_TRUE(kg_at_mouse_parse("0,0,1", &m));
  TEST_ASSERT_EQUAL_INT(1, m.buttons);
  TEST_ASSERT_TRUE(kg_at_mouse_parse("1,2,3,-4", &m));
  TEST_ASSERT_EQUAL_INT(3, m.buttons);
  TEST_ASSERT_EQUAL_INT(-4, m.wheel);
  TEST_ASSERT_TRUE(kg_at_mouse_parse("1,2,,1", &m));
  TEST_ASSERT_EQUAL_INT(KG_AT_MOUSE_KEEP, m.buttons);
  TEST_ASSERT_EQUAL_INT(1, m.wheel);
}

static void test_mouse_parse_bad() {
  kg_at_mouse_t m;
  const char *bad[] = {"", "5", "5,", ",5", "a,b", "5,5x", "5,5,", "5,5,1,", "5,5,1,1,1", "4001,0",
                       "0,-4001", "0,0,32", "0,0,0,128"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    TEST_ASSERT_FALSE_MESSAGE(kg_at_mouse_parse(bad[i], &m), bad[i]);
  }
}

static void test_mouse_steps() {
  TEST_ASSERT_EQUAL_INT(1, kg_at_mouse_steps(0, 0));
  TEST_ASSERT_EQUAL_INT(1, kg_at_mouse_steps(127, -127));
  TEST_ASSERT_EQUAL_INT(2, kg_at_mouse_steps(128, 0));
  TEST_ASSERT_EQUAL_INT(3, kg_at_mouse_steps(0, -255));
  int x = 300, y = -130, steps = kg_at_mouse_steps(x, y), sx = 0, sy = 0;
  for (int i = 0; i < steps; i++) {
    sx += kg_at_mouse_step(&x);
    sy += kg_at_mouse_step(&y);
  }
  TEST_ASSERT_EQUAL_INT(300, sx);
  TEST_ASSERT_EQUAL_INT(-130, sy);
  TEST_ASSERT_EQUAL_INT(0, x);
  TEST_ASSERT_EQUAL_INT(0, y);
}

static void test_mouse_button() {
  uint8_t b;
  TEST_ASSERT_TRUE(kg_at_mouse_button("", &b)); TEST_ASSERT_EQUAL_UINT8(1, b);
  TEST_ASSERT_TRUE(kg_at_mouse_button("RIGHT", &b)); TEST_ASSERT_EQUAL_UINT8(2, b);
  TEST_ASSERT_TRUE(kg_at_mouse_button("middle", &b)); TEST_ASSERT_EQUAL_UINT8(4, b);
  TEST_ASSERT_FALSE(kg_at_mouse_button("x", &b));
}

static void test_pad() {
  uint16_t b;
  TEST_ASSERT_TRUE(kg_at_pad_parse("up", &b)); TEST_ASSERT_EQUAL_UINT16(HID_SC_UP, b);
  TEST_ASSERT_TRUE(kg_at_pad_parse("Start+a+MODE", &b));
  TEST_ASSERT_EQUAL_UINT16(HID_SC_START | HID_SC_A | HID_SC_MODE, b);
  const char *bad[] = {"", "+", "a+", "+a", "q", "ab", "a b"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    TEST_ASSERT_FALSE_MESSAGE(kg_at_pad_parse(bad[i], &b), bad[i]);
  }
}

static void split_ok(const char *path, const char *dir, const char *name) {
  char d[16], n[16];
  TEST_ASSERT_TRUE_MESSAGE(kg_at_path_split(path, d, sizeof(d), n, sizeof(n)), path);
  TEST_ASSERT_EQUAL_STRING(dir, d);
  TEST_ASSERT_EQUAL_STRING(name, n);
}

static void test_path_split() {
  split_ok("/games/x.trd", "/games", "x.trd");
  split_ok("/a/b/c", "/a/b", "c");
  split_ok("/x.trd", "/", "x.trd");
  split_ok("x.trd", "/", "x.trd");
  char d[8], n[8];
  TEST_ASSERT_FALSE(kg_at_path_split("", d, sizeof(d), n, sizeof(n)));
  TEST_ASSERT_FALSE(kg_at_path_split("/games/", d, sizeof(d), n, sizeof(n)));
  TEST_ASSERT_FALSE(kg_at_path_split("/x/12345678", d, sizeof(d), n, sizeof(n)));
  TEST_ASSERT_FALSE(kg_at_path_split("/1234567/x", d, sizeof(d), n, sizeof(n)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_mouse_parse);
  RUN_TEST(test_mouse_parse_bad);
  RUN_TEST(test_mouse_steps);
  RUN_TEST(test_mouse_button);
  RUN_TEST(test_pad);
  RUN_TEST(test_path_split);
  return UNITY_END();
}
