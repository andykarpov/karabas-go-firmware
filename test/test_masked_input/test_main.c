#include <unity.h>
#include "masked_input.h"

void setUp() {}
void tearDown() {}

static void test_field_typing() {
  kg_field_t f;
  kg_field_init(&f, "01.01.2000");
  TEST_ASSERT_EQUAL(0, f.pos);
  TEST_ASSERT_FALSE(kg_field_type(&f, '2'));
  TEST_ASSERT_FALSE(kg_field_type(&f, '5'));
  TEST_ASSERT_EQUAL(3, f.pos); // skipped the dot
  const char *rest = "10202";
  for (const char *c = rest; *c; c++) TEST_ASSERT_FALSE(kg_field_type(&f, *c));
  TEST_ASSERT_EQUAL(9, f.pos);
  TEST_ASSERT_TRUE(kg_field_type(&f, '6')); // the last digit
  TEST_ASSERT_EQUAL(9, f.pos);
  TEST_ASSERT_EQUAL_STRING("25.10.2026", f.text);
  TEST_ASSERT_FALSE(kg_field_type(&f, 'x'));
  TEST_ASSERT_EQUAL_STRING("25.10.2026", f.text);
}

static void test_field_cursor() {
  kg_field_t f;
  kg_field_init(&f, "14:37");
  kg_field_left(&f);
  TEST_ASSERT_EQUAL(0, f.pos);
  kg_field_right(&f);
  kg_field_right(&f);
  TEST_ASSERT_EQUAL(3, f.pos);
  kg_field_right(&f);
  kg_field_right(&f);
  TEST_ASSERT_EQUAL(4, f.pos);
  kg_field_left(&f);
  kg_field_left(&f);
  TEST_ASSERT_EQUAL(1, f.pos);
}

static void test_field_turn() {
  kg_field_t f;
  kg_field_init(&f, "09:30");
  kg_field_turn(&f, -1);
  TEST_ASSERT_EQUAL_STRING("99:30", f.text);
  kg_field_turn(&f, 1);
  kg_field_turn(&f, 1);
  TEST_ASSERT_EQUAL_STRING("19:30", f.text);
  kg_field_right(&f);
  kg_field_turn(&f, 12);
  TEST_ASSERT_EQUAL_STRING("11:30", f.text);
}

static void test_text_turn() {
  char t[4] = "";
  kg_text_turn(t, 3, -1); // an empty text gets the first char
  TEST_ASSERT_EQUAL_STRING("a", t);
  kg_text_turn(t, 3, 1);
  TEST_ASSERT_EQUAL_STRING("b", t);
  kg_text_turn(t, 3, -2); // round
  TEST_ASSERT_EQUAL_STRING("~", t);
  kg_text_turn(t, 3, 1);
  TEST_ASSERT_EQUAL_STRING("a", t);
  kg_text_add(t, 3);
  kg_text_turn(t, 3, 26);
  TEST_ASSERT_EQUAL_STRING("aA", t);
  kg_text_add(t, 3);
  kg_text_add(t, 3); // full
  TEST_ASSERT_EQUAL_STRING("aAa", t);
  t[2] = '\t'; // not in the chars: from the first one
  kg_text_turn(t, 3, 1);
  TEST_ASSERT_EQUAL_STRING("aAb", t);
}

static void test_date_parse() {
  int d, m, y;
  TEST_ASSERT_TRUE(kg_date_parse("25.10.2026", &d, &m, &y));
  TEST_ASSERT_EQUAL(25, d);
  TEST_ASSERT_EQUAL(10, m);
  TEST_ASSERT_EQUAL(2026, y);
  TEST_ASSERT_TRUE(kg_date_parse("29.02.2024", &d, &m, &y));
  TEST_ASSERT_FALSE(kg_date_parse("29.02.2026", &d, &m, &y));
  TEST_ASSERT_FALSE(kg_date_parse("31.04.2026", &d, &m, &y));
  TEST_ASSERT_FALSE(kg_date_parse("00.01.2026", &d, &m, &y));
  TEST_ASSERT_FALSE(kg_date_parse("01.13.2026", &d, &m, &y));
  TEST_ASSERT_FALSE(kg_date_parse("01.01.1999", &d, &m, &y));
  TEST_ASSERT_FALSE(kg_date_parse("01.01.2100", &d, &m, &y));
  TEST_ASSERT_FALSE(kg_date_parse("01/01/2026", &d, &m, &y));
}

static void test_time_parse() {
  int h, m;
  TEST_ASSERT_TRUE(kg_time_parse("23:59", &h, &m));
  TEST_ASSERT_EQUAL(23, h);
  TEST_ASSERT_EQUAL(59, m);
  TEST_ASSERT_TRUE(kg_time_parse("00:00", &h, &m));
  TEST_ASSERT_FALSE(kg_time_parse("24:00", &h, &m));
  TEST_ASSERT_FALSE(kg_time_parse("12:60", &h, &m));
  TEST_ASSERT_FALSE(kg_time_parse("12.30", &h, &m));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_field_typing);
  RUN_TEST(test_field_cursor);
  RUN_TEST(test_field_turn);
  RUN_TEST(test_text_turn);
  RUN_TEST(test_date_parse);
  RUN_TEST(test_time_parse);
  return UNITY_END();
}
