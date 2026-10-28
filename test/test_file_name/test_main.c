#include <unity.h>
#include <string.h>
#include "file_name.h"

void setUp() {}
void tearDown() {}

static void test_has_ext() {
  TEST_ASSERT_TRUE(kg_name_has_ext("Game.TRD", ".trd,.scl"));
  TEST_ASSERT_TRUE(kg_name_has_ext("game.scl", ".TRD,.SCL"));
  TEST_ASSERT_FALSE(kg_name_has_ext("game.tap", ".trd,.scl"));
  TEST_ASSERT_TRUE(kg_name_has_ext("anything", ""));
  TEST_ASSERT_FALSE(kg_name_has_ext("a.b", ".trd"));
  TEST_ASSERT_FALSE(kg_name_has_ext("", ".trd"));
}

static void test_short_name() {
  char out[11];
  kg_short_name("GAME.TRD", out, 10);
  TEST_ASSERT_EQUAL_STRING("GAME.TRD", out);
  kg_short_name("Prince of Persia.trd", out, 10);
  TEST_ASSERT_EQUAL_STRING("Princ~.trd", out);
  kg_short_name("Prince of Persia", out, 10);
  TEST_ASSERT_EQUAL_STRING("Prince of~", out);
  kg_short_name("Some long name.backup", out, 10);
  TEST_ASSERT_EQUAL_STRING("Some long~", out);
  kg_short_name(".hidden_file_name", out, 10);
  TEST_ASSERT_EQUAL_STRING(".hidden_f~", out);
  kg_short_name("1234567890", out, 10);
  TEST_ASSERT_EQUAL_STRING("1234567890", out);
  kg_short_name("12345678901", out, 10);
  TEST_ASSERT_EQUAL_STRING("123456789~", out);
}

static void test_phys() {
  char out[8];
  kg_phys_name(0, out);
  TEST_ASSERT_EQUAL_STRING(":phys0", out);
  kg_phys_name(1, out);
  TEST_ASSERT_EQUAL_STRING(":phys1", out);
  TEST_ASSERT_EQUAL_INT(0, kg_phys_drive(":phys0"));
  TEST_ASSERT_EQUAL_INT(1, kg_phys_drive(":phys1"));
  TEST_ASSERT_EQUAL_INT(-1, kg_phys_drive(":phys2"));
  TEST_ASSERT_EQUAL_INT(-1, kg_phys_drive(":phys10"));
  TEST_ASSERT_EQUAL_INT(-1, kg_phys_drive("phys0.adf"));
  TEST_ASSERT_EQUAL_INT(-1, kg_phys_drive(""));
}

static void test_trim_lower() {
  char s[32];
  strcpy(s, "  Hello World \r\n"); kg_trim(s); TEST_ASSERT_EQUAL_STRING("Hello World", s);
  strcpy(s, "x"); kg_trim(s); TEST_ASSERT_EQUAL_STRING("x", s);
  strcpy(s, " \t "); kg_trim(s); TEST_ASSERT_EQUAL_STRING("", s);
  strcpy(s, ""); kg_trim(s); TEST_ASSERT_EQUAL_STRING("", s);
  strcpy(s, "Game.TRD"); kg_str_lower(s); TEST_ASSERT_EQUAL_STRING("game.trd", s);
}

static void test_paths() {
  char p[16];
  TEST_ASSERT_TRUE(kg_path_join(p, sizeof(p), "/games", "x.trd"));
  TEST_ASSERT_EQUAL_STRING("/games/x.trd", p);
  TEST_ASSERT_TRUE(kg_path_join(p, sizeof(p), "/", "x.trd"));
  TEST_ASSERT_EQUAL_STRING("//x.trd", p); // as String did; SdFat takes it
  TEST_ASSERT_FALSE(kg_path_join(p, sizeof(p), "/games/zx", "long name.trd"));
  strcpy(p, "//games//x"); kg_path_collapse(p); TEST_ASSERT_EQUAL_STRING("/games/x", p);
  strcpy(p, ""); kg_path_root(p, sizeof(p)); TEST_ASSERT_EQUAL_STRING("/", p);
  strcpy(p, "games"); kg_path_root(p, sizeof(p)); TEST_ASSERT_EQUAL_STRING("/games", p);
  strcpy(p, "/games"); kg_path_root(p, sizeof(p)); TEST_ASSERT_EQUAL_STRING("/games", p);
  strcpy(p, "0123456789abcde"); kg_path_root(p, sizeof(p)); TEST_ASSERT_EQUAL_STRING("/0123456789abcd", p);
  strcpy(p, "/games/zx"); kg_path_parent(p); TEST_ASSERT_EQUAL_STRING("/games", p);
  kg_path_parent(p); TEST_ASSERT_EQUAL_STRING("", p);
  strcpy(p, "games"); kg_path_parent(p); TEST_ASSERT_EQUAL_STRING("games", p);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_has_ext);
  RUN_TEST(test_short_name);
  RUN_TEST(test_phys);
  RUN_TEST(test_trim_lower);
  RUN_TEST(test_paths);
  return UNITY_END();
}
