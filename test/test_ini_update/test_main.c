#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "ini_update.h"

void setUp() {}
void tearDown() {}

static char out[2048];

static void collect(void *, const char *line) {
  strcat(out, line);
  strcat(out, "\n");
}

// runs the update over the lines of text ('\n' separated), the result is in out
static void run(const char *text, kg_ini_item_t *items, uint8_t count) {
  out[0] = '\0';
  kg_ini_update_t u;
  kg_ini_update_begin(&u, items, count, collect, NULL);
  char line[KG_INI_LINE_MAX + 1];
  const char *p = text;
  while (*p) {
    const char *e = strchr(p, '\n');
    size_t len = e ? (size_t) (e - p) : strlen(p);
    memcpy(line, p, len);
    line[len] = '\0';
    kg_ini_update_line(&u, line);
    p += len + (e ? 1 : 0);
  }
  kg_ini_update_end(&u);
}

static const char *ini =
  "; karabas settings\n"
  "[setup]\n"
  "debug=0\n"
  "burst=1\n"
  "\n"
  "[ft812]\n"
  "enabled=1\n"
  "color_bg=010101\n"
  "\n"
  "[autoload]\n"
  "enabled=1\n"
  "core=tsconf\n";

static void test_replace() {
  kg_ini_item_t items[] = { { "setup", "debug", "1", false }, { "autoload", "enabled", "0", false } };
  run(ini, items, 2);
  TEST_ASSERT_EQUAL_STRING(
    "; karabas settings\n"
    "[setup]\n"
    "debug=1\n"
    "burst=1\n"
    "\n"
    "[ft812]\n"
    "enabled=1\n"
    "color_bg=010101\n"
    "\n"
    "[autoload]\n"
    "enabled=0\n"
    "core=tsconf\n", out);
  TEST_ASSERT_TRUE(items[0].done);
  TEST_ASSERT_TRUE(items[1].done);
}

static void test_case_and_spaces() {
  kg_ini_item_t items[] = { { "setup", "debug", "1", false } };
  run("  [ Setup ]\n  Debug = 0\n", items, 1);
  TEST_ASSERT_EQUAL_STRING("  [ Setup ]\ndebug=1\n", out);
}

static void test_add_key() {
  kg_ini_item_t items[] = { { "setup", "dvi_only", "1", false }, { "autoload", "timeout", "5", false } };
  run(ini, items, 2);
  TEST_ASSERT_EQUAL_STRING(
    "; karabas settings\n"
    "[setup]\n"
    "debug=0\n"
    "burst=1\n"
    "dvi_only=1\n"
    "\n"
    "[ft812]\n"
    "enabled=1\n"
    "color_bg=010101\n"
    "\n"
    "[autoload]\n"
    "enabled=1\n"
    "core=tsconf\n"
    "timeout=5\n", out);
}

static void test_add_section() {
  kg_ini_item_t items[] = {
    { "ft812", "video_mode", "3", false },
    { "autoload", "enabled", "1", false },
    { "ft812", "enabled", "0", false },
    { "autoload", "core", "boot", false },
  };
  run("[setup]\ndebug=0\n\n", items, 4);
  TEST_ASSERT_EQUAL_STRING(
    "[setup]\n"
    "debug=0\n"
    "\n"
    "[ft812]\n"
    "video_mode=3\n"
    "enabled=0\n"
    "\n"
    "[autoload]\n"
    "enabled=1\n"
    "core=boot\n", out);
}

static void test_add_section_no_blank() {
  kg_ini_item_t items[] = { { "autoload", "core", "boot", false } };
  run("[setup]\ndebug=0\n", items, 1);
  TEST_ASSERT_EQUAL_STRING("[setup]\ndebug=0\n\n[autoload]\ncore=boot\n", out);
}

static void test_empty_file() {
  kg_ini_item_t items[] = { { "setup", "debug", "1", false } };
  run("", items, 1);
  TEST_ASSERT_EQUAL_STRING("[setup]\ndebug=1\n", out);
}

static void test_other_sections_untouched() {
  // enabled of [ft812] is not the one of [autoload]; keys before the first section and
  // comments are kept
  kg_ini_item_t items[] = { { "autoload", "enabled", "0", false } };
  run("enabled=1\n[ft812]\nenabled=1\n;enabled=1\n# x=1\n[autoload]\nenabled = 1\n", items, 1);
  TEST_ASSERT_EQUAL_STRING("enabled=1\n[ft812]\nenabled=1\n;enabled=1\n# x=1\n[autoload]\nenabled=0\n", out);
}

static void test_duplicates() {
  kg_ini_item_t items[] = { { "setup", "debug", "1", false } };
  run("[setup]\ndebug=0\ndebug=0\n[setup]\ndebug=0\n", items, 1);
  TEST_ASSERT_EQUAL_STRING("[setup]\ndebug=1\ndebug=1\n[setup]\ndebug=1\n", out);
}

static void test_no_items() {
  run(ini, NULL, 0);
  TEST_ASSERT_EQUAL_STRING(ini, out);
}

// a value longer than the settings ones: the file loader dir of a core
static void test_long_value() {
  const char *dir = "/games/nes/Japan/Role Playing Games (translated)";
  kg_ini_item_t items[] = { { "fileloader", "nes", dir, false } };
  run("[setup]\ndebug=1\n", items, 1);
  TEST_ASSERT_EQUAL_STRING(
    "[setup]\n"
    "debug=1\n"
    "\n"
    "[fileloader]\n"
    "nes=/games/nes/Japan/Role Playing Games (translated)\n", out);
}

static void test_line_fits() {
  char v[80];
  memset(v, 'a', sizeof(v));
  v[KG_INI_LINE_MAX - 4] = '\0'; // "nes=" + 74
  TEST_ASSERT_TRUE(kg_ini_line_fits("nes", v));
  v[KG_INI_LINE_MAX - 4] = 'a';
  v[KG_INI_LINE_MAX - 3] = '\0';
  TEST_ASSERT_FALSE(kg_ini_line_fits("nes", v));
}

// value NULL: the key goes (all its lines), a key that is not there is not added
static void test_remove() {
  kg_ini_item_t items[] = { { "setup", "burst", NULL, false }, { "setup", "nothere", NULL, false },
                            { "osd", "video", NULL, false } };
  run("[setup]\ndebug=0\nburst=1\nburst=0\n\n[osd]\nvideo=ntsc\njoy=swap\n", items, 3);
  TEST_ASSERT_EQUAL_STRING("[setup]\ndebug=0\n\n[osd]\njoy=swap\n", out);
}

// a path longer than a karabas.ini line
static void test_long_path() {
  char path[300];
  memset(path, 'a', sizeof(path) - 1);
  path[0] = '/';
  path[sizeof(path) - 1] = '\0';
  kg_ini_item_t items[] = { { "file", "disk_a", path, false } };
  run("", items, 1);
  char want[400];
  snprintf(want, sizeof(want), "[file]\ndisk_a=%s\n", path);
  TEST_ASSERT_EQUAL_STRING(want, out);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_replace);
  RUN_TEST(test_case_and_spaces);
  RUN_TEST(test_add_key);
  RUN_TEST(test_add_section);
  RUN_TEST(test_add_section_no_blank);
  RUN_TEST(test_empty_file);
  RUN_TEST(test_other_sections_untouched);
  RUN_TEST(test_duplicates);
  RUN_TEST(test_no_items);
  RUN_TEST(test_long_value);
  RUN_TEST(test_line_fits);
  RUN_TEST(test_remove);
  RUN_TEST(test_long_path);
  return UNITY_END();
}
