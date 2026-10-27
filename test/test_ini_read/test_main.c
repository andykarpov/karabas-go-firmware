#include <unity.h>
#include <string.h>
#include "ini_read.h"

void setUp() {}
void tearDown() {}

static const char ini[] =
  "; karabas.ini\r\n"
  "[setup]\r\n"
  "debug = 1\r\n"
  "  tz=+03:00  \r\n"
  "# a comment = 5\r\n"
  "name = a ; b\r\n"
  "\r\n"
  "[ FT812 ]\n"
  "Enabled=yes\n"
  "video_mode = 2\n"
  "nokey\n"
  "[matrix]\n"
  "enabled = off\n"
  "brightness = 0";

static bool get(const char *s, const char *k, char *out, size_t n) {
  return kg_ini_get(ini, strlen(ini), s, k, out, n);
}

static void test_get() {
  char v[32];
  TEST_ASSERT_TRUE(get("setup", "debug", v, sizeof(v))); TEST_ASSERT_EQUAL_STRING("1", v);
  TEST_ASSERT_TRUE(get("setup", "tz", v, sizeof(v))); TEST_ASSERT_EQUAL_STRING("+03:00", v);
  TEST_ASSERT_TRUE(get("setup", "name", v, sizeof(v))); TEST_ASSERT_EQUAL_STRING("a ; b", v);
  TEST_ASSERT_TRUE(get("ft812", "enabled", v, sizeof(v))); TEST_ASSERT_EQUAL_STRING("yes", v);
  TEST_ASSERT_TRUE(get("FT812", "VIDEO_MODE", v, sizeof(v))); TEST_ASSERT_EQUAL_STRING("2", v);
  TEST_ASSERT_TRUE(get("matrix", "brightness", v, sizeof(v))); TEST_ASSERT_EQUAL_STRING("0", v); // no line end
  // not there: a comment, a key of another section, a missing section, a line without '='
  TEST_ASSERT_FALSE(get("setup", "# a comment", v, sizeof(v)));
  TEST_ASSERT_FALSE(get("setup", "a comment", v, sizeof(v)));
  TEST_ASSERT_FALSE(get("setup", "enabled", v, sizeof(v)));
  TEST_ASSERT_FALSE(get("autoload", "enabled", v, sizeof(v)));
  TEST_ASSERT_FALSE(get("ft812", "nokey", v, sizeof(v)));
  // the value does not fit
  TEST_ASSERT_FALSE(get("setup", "tz", v, 6));
  TEST_ASSERT_TRUE(get("setup", "tz", v, 7));
}

static void test_first_wins() {
  const char *t = "[a]\nk=1\nk=2\n[a]\nj=3\n";
  char v[8];
  TEST_ASSERT_TRUE(kg_ini_get(t, strlen(t), "a", "k", v, sizeof(v))); TEST_ASSERT_EQUAL_STRING("1", v);
  // only the first [a] is read, as IniFile did
  TEST_ASSERT_FALSE(kg_ini_get(t, strlen(t), "a", "j", v, sizeof(v)));
}

static void test_bool() {
  bool b = false;
  TEST_ASSERT_TRUE(kg_ini_get_bool(ini, strlen(ini), "setup", "debug", &b)); TEST_ASSERT_TRUE(b);
  TEST_ASSERT_TRUE(kg_ini_get_bool(ini, strlen(ini), "ft812", "enabled", &b)); TEST_ASSERT_TRUE(b);
  b = true;
  TEST_ASSERT_FALSE(kg_ini_get_bool(ini, strlen(ini), "matrix", "enabled", &b)); // "off"
  TEST_ASSERT_TRUE(b);
  const char *t = "[s]\na=FALSE\nb=No\nc=0\nd=TRUE\n";
  TEST_ASSERT_TRUE(kg_ini_get_bool(t, strlen(t), "s", "a", &b)); TEST_ASSERT_FALSE(b);
  TEST_ASSERT_TRUE(kg_ini_get_bool(t, strlen(t), "s", "b", &b)); TEST_ASSERT_FALSE(b);
  TEST_ASSERT_TRUE(kg_ini_get_bool(t, strlen(t), "s", "c", &b)); TEST_ASSERT_FALSE(b);
  TEST_ASSERT_TRUE(kg_ini_get_bool(t, strlen(t), "s", "d", &b)); TEST_ASSERT_TRUE(b);
}

static void test_valid() {
  char t[200];
  // 78 chars and a line end fit an 80-byte buffer, 79 do not
  memset(t, 'x', 78); strcpy(t + 78, "\nk=1\n");
  TEST_ASSERT_TRUE(kg_ini_valid(t, strlen(t), 80));
  memset(t, 'x', 79); strcpy(t + 79, "\nk=1\n");
  TEST_ASSERT_FALSE(kg_ini_valid(t, strlen(t), 80));
  memset(t, 'x', 78); strcpy(t + 78, "\r\nk=1\r\n");
  TEST_ASSERT_TRUE(kg_ini_valid(t, strlen(t), 80));
  // the last line without a line end may fill the buffer
  memset(t, 'x', 80); t[80] = '\0';
  TEST_ASSERT_TRUE(kg_ini_valid(t, strlen(t), 80));
  memset(t, 'x', 81); t[81] = '\0';
  TEST_ASSERT_FALSE(kg_ini_valid(t, strlen(t), 80));
  TEST_ASSERT_TRUE(kg_ini_valid(ini, strlen(ini), 80));
  TEST_ASSERT_TRUE(kg_ini_valid("", 0, 80));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_get);
  RUN_TEST(test_first_wins);
  RUN_TEST(test_bool);
  RUN_TEST(test_valid);
  return UNITY_END();
}
