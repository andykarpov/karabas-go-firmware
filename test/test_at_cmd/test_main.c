#include <unity.h>
#include <string.h>
#include "at_cmd.h"

void setUp() {}
void tearDown() {}

// feeds s, returns the forwarded bytes; the args of the commands go to cmds, separated by '|'
static const char *feed(kg_at_parser_t *p, const char *s, char *cmds) {
  static char fwd[128];
  size_t n = 0;
  for (; *s; s++) {
    kg_at_result_t r = kg_at_feed(p, *s);
    if (r == KG_AT_PASS) { memcpy(fwd + n, p->out, p->out_len); n += p->out_len; }
    if (r == KG_AT_COMMAND) { if (*cmds) strcat(cmds, "|"); strcat(cmds, kg_at_arg(p)); }
  }
  fwd[n] = '\0';
  return fwd;
}

static void test_command() {
  kg_at_parser_t p; kg_at_init(&p);
  char cmds[64] = "";
  TEST_ASSERT_EQUAL_STRING("", feed(&p, "AT+KARABAS=list\r\n", cmds));
  TEST_ASSERT_EQUAL_STRING("list", cmds);
  cmds[0] = '\0';
  TEST_ASSERT_EQUAL_STRING("", feed(&p, "at+karabas=MSC\n", cmds));
  TEST_ASSERT_EQUAL_STRING("MSC", cmds);
  cmds[0] = '\0';
  TEST_ASSERT_EQUAL_STRING("", feed(&p, "AT+KARABAS=\r", cmds));
  TEST_ASSERT_EQUAL_STRING("", cmds);
}

static void test_passthrough() {
  kg_at_parser_t p; kg_at_init(&p);
  char cmds[64] = "";
  TEST_ASSERT_EQUAL_STRING("AT+CWJAP=\"x\"\r\n", feed(&p, "AT+CWJAP=\"x\"\r\n", cmds));
  TEST_ASSERT_EQUAL_STRING("hello\n", feed(&p, "hello\n", cmds));
  // a held 'A' that turns out to start a command
  TEST_ASSERT_EQUAL_STRING("A", feed(&p, "AAT+KARABAS=boot\r", cmds));
  TEST_ASSERT_EQUAL_STRING("boot", cmds);
  // a LF that does not follow a command goes through
  TEST_ASSERT_EQUAL_STRING("x\n", feed(&p, "x\n", cmds));
  TEST_ASSERT_EQUAL_STRING("boot", cmds);
}

static void test_too_long() {
  kg_at_parser_t p; kg_at_init(&p);
  char cmds[64] = "";
  const char *s = "AT+KARABAS=0123456789abcdefXYZ\r";
  TEST_ASSERT_EQUAL_STRING(s, feed(&p, s, cmds));
  TEST_ASSERT_EQUAL_STRING("", cmds);
}

static void test_flush() {
  kg_at_parser_t p; kg_at_init(&p);
  char cmds[64] = "";
  TEST_ASSERT_EQUAL_STRING("", feed(&p, "AT+KA", cmds));
  TEST_ASSERT_EQUAL_UINT8(5, kg_at_flush(&p));
  TEST_ASSERT_EQUAL_MEMORY("AT+KA", p.out, 5);
  TEST_ASSERT_EQUAL_UINT8(0, kg_at_flush(&p));
  TEST_ASSERT_EQUAL_STRING("q", feed(&p, "q", cmds));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_command);
  RUN_TEST(test_passthrough);
  RUN_TEST(test_too_long);
  RUN_TEST(test_flush);
  return UNITY_END();
}
