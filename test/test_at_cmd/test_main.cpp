#include <unity.h>
#include <string.h>
#include <string>
#include "at_cmd.h"

void setUp() {}
void tearDown() {}

// feeds s, returns the forwarded bytes; the args of the commands go to cmds, separated by '|'
static std::string feed(kg_at_parser_t *p, const char *s, std::string *cmds) {
  std::string fwd;
  for (; *s; s++) {
    kg_at_result_t r = kg_at_feed(p, *s);
    if (r == KG_AT_PASS) fwd.append(p->out, p->out_len);
    if (r == KG_AT_COMMAND) { if (!cmds->empty()) *cmds += "|"; *cmds += kg_at_arg(p); }
  }
  return fwd;
}

static void test_command() {
  kg_at_parser_t p; kg_at_init(&p);
  std::string cmds;
  TEST_ASSERT_EQUAL_STRING("", feed(&p, "AT+KARABAS=list\r\n", &cmds).c_str());
  TEST_ASSERT_EQUAL_STRING("list", cmds.c_str());
  cmds.clear();
  TEST_ASSERT_EQUAL_STRING("", feed(&p, "at+karabas=MSC\n", &cmds).c_str());
  TEST_ASSERT_EQUAL_STRING("MSC", cmds.c_str());
  cmds.clear();
  TEST_ASSERT_EQUAL_STRING("", feed(&p, "AT+KARABAS=\r", &cmds).c_str());
  TEST_ASSERT_EQUAL_STRING("", cmds.c_str());
}

static void test_passthrough() {
  kg_at_parser_t p; kg_at_init(&p);
  std::string cmds;
  TEST_ASSERT_EQUAL_STRING("AT+CWJAP=\"x\"\r\n", feed(&p, "AT+CWJAP=\"x\"\r\n", &cmds).c_str());
  TEST_ASSERT_EQUAL_STRING("hello\n", feed(&p, "hello\n", &cmds).c_str());
  // a held 'A' that turns out to start a command
  TEST_ASSERT_EQUAL_STRING("A", feed(&p, "AAT+KARABAS=boot\r", &cmds).c_str());
  TEST_ASSERT_EQUAL_STRING("boot", cmds.c_str());
  // a LF that does not follow a command goes through
  TEST_ASSERT_EQUAL_STRING("x\n", feed(&p, "x\n", &cmds).c_str());
  TEST_ASSERT_TRUE(cmds == "boot");
}

static void test_too_long() {
  kg_at_parser_t p; kg_at_init(&p);
  std::string cmds;
  const char *s = "AT+KARABAS=0123456789abcdefXYZ\r";
  TEST_ASSERT_EQUAL_STRING(s, feed(&p, s, &cmds).c_str());
  TEST_ASSERT_TRUE(cmds.empty());
}

static void test_flush() {
  kg_at_parser_t p; kg_at_init(&p);
  std::string cmds;
  TEST_ASSERT_EQUAL_STRING("", feed(&p, "AT+KA", &cmds).c_str());
  TEST_ASSERT_EQUAL_UINT8(5, kg_at_flush(&p));
  TEST_ASSERT_EQUAL_MEMORY("AT+KA", p.out, 5);
  TEST_ASSERT_EQUAL_UINT8(0, kg_at_flush(&p));
  TEST_ASSERT_EQUAL_STRING("q", feed(&p, "q", &cmds).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_command);
  RUN_TEST(test_passthrough);
  RUN_TEST(test_too_long);
  RUN_TEST(test_flush);
  return UNITY_END();
}
