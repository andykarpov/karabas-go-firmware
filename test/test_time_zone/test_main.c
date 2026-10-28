#include <unity.h>
#include <string.h>
#include "time_zone.h"

void setUp() {}
void tearDown() {}

static void test_parse() {
  int m = 1;
  TEST_ASSERT_TRUE(kg_tz_parse("+03:00", &m)); TEST_ASSERT_EQUAL(180, m);
  TEST_ASSERT_TRUE(kg_tz_parse("3", &m)); TEST_ASSERT_EQUAL(180, m);
  TEST_ASSERT_TRUE(kg_tz_parse("-5", &m)); TEST_ASSERT_EQUAL(-300, m);
  TEST_ASSERT_TRUE(kg_tz_parse("5:30", &m)); TEST_ASSERT_EQUAL(330, m);
  TEST_ASSERT_TRUE(kg_tz_parse("+05:45", &m)); TEST_ASSERT_EQUAL(345, m);
  TEST_ASSERT_TRUE(kg_tz_parse("-03:30", &m)); TEST_ASSERT_EQUAL(-210, m);
  TEST_ASSERT_TRUE(kg_tz_parse("0", &m)); TEST_ASSERT_EQUAL(0, m);
  TEST_ASSERT_TRUE(kg_tz_parse("+14:00", &m)); TEST_ASSERT_EQUAL(840, m);
  TEST_ASSERT_TRUE(kg_tz_parse("-12:00", &m)); TEST_ASSERT_EQUAL(-720, m);
  m = 7;
  TEST_ASSERT_FALSE(kg_tz_parse("", &m));
  TEST_ASSERT_FALSE(kg_tz_parse("+", &m));
  TEST_ASSERT_FALSE(kg_tz_parse("+15", &m));
  TEST_ASSERT_FALSE(kg_tz_parse("-13", &m));
  TEST_ASSERT_FALSE(kg_tz_parse("+03:10", &m));
  TEST_ASSERT_FALSE(kg_tz_parse("+03:60", &m));
  TEST_ASSERT_FALSE(kg_tz_parse("+03:0", &m));
  TEST_ASSERT_FALSE(kg_tz_parse("+123", &m));
  TEST_ASSERT_FALSE(kg_tz_parse("UTC+3", &m));
  TEST_ASSERT_FALSE(kg_tz_parse("3 ", &m));
  TEST_ASSERT_EQUAL(7, m);
}

static void test_format() {
  char buf[8];
  kg_tz_format(180, buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("+03:00", buf);
  kg_tz_format(-270, buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("-04:30", buf);
  kg_tz_format(0, buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("+00:00", buf);
  kg_tz_format(-15, buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("-00:15", buf);
  int m;
  TEST_ASSERT_TRUE(kg_tz_parse("-00:15", &m)); TEST_ASSERT_EQUAL(-15, m);
}

static void test_step() {
  TEST_ASSERT_EQUAL(195, kg_tz_step(180, 1));
  TEST_ASSERT_EQUAL(165, kg_tz_step(180, -1));
  TEST_ASSERT_EQUAL(KG_TZ_MIN, kg_tz_step(KG_TZ_MAX, 1));
  TEST_ASSERT_EQUAL(KG_TZ_MAX, kg_tz_step(KG_TZ_MIN, -1));
  TEST_ASSERT_EQUAL(-15, kg_tz_step(0, -1));
}

static void check(const kg_datetime_t *t, int y, int mo, int d, int h, int mi, int s) {
  TEST_ASSERT_EQUAL(y, t->year); TEST_ASSERT_EQUAL(mo, t->month); TEST_ASSERT_EQUAL(d, t->day);
  TEST_ASSERT_EQUAL(h, t->hour); TEST_ASSERT_EQUAL(mi, t->minute); TEST_ASSERT_EQUAL(s, t->second);
}

static void test_add() {
  kg_datetime_t t = { 2026, 10, 26, 18, 46, 23 };
  kg_datetime_add_minutes(&t, 180); check(&t, 2026, 10, 26, 21, 46, 23);
  kg_datetime_add_minutes(&t, 180); check(&t, 2026, 10, 27, 0, 46, 23);
  kg_datetime_add_minutes(&t, -360); check(&t, 2026, 10, 26, 18, 46, 23);
  t = (kg_datetime_t) { 2026, 12, 31, 22, 30, 0 };
  kg_datetime_add_minutes(&t, 90); check(&t, 2027, 1, 1, 0, 0, 0);
  t = (kg_datetime_t) { 2027, 1, 1, 0, 10, 5 };
  kg_datetime_add_minutes(&t, -15); check(&t, 2026, 12, 31, 23, 55, 5);
  // leap years
  t = (kg_datetime_t) { 2028, 2, 28, 23, 0, 0 };
  kg_datetime_add_minutes(&t, 60); check(&t, 2028, 2, 29, 0, 0, 0);
  t = (kg_datetime_t) { 2027, 2, 28, 23, 0, 0 };
  kg_datetime_add_minutes(&t, 60); check(&t, 2027, 3, 1, 0, 0, 0);
  t = (kg_datetime_t) { 2028, 3, 1, 1, 0, 0 };
  kg_datetime_add_minutes(&t, -720); check(&t, 2028, 2, 29, 13, 0, 0);
  t = (kg_datetime_t) { 2000, 3, 1, 0, 0, 0 };
  kg_datetime_add_minutes(&t, -1); check(&t, 2000, 2, 29, 23, 59, 0);
  t = (kg_datetime_t) { 2026, 5, 10, 12, 0, 0 };
  kg_datetime_add_minutes(&t, 0); check(&t, 2026, 5, 10, 12, 0, 0);
}

static void test_valid() {
  kg_datetime_t t = { 2026, 10, 27, 23, 59, 59 };
  TEST_ASSERT_TRUE(kg_datetime_valid(&t));
  t = (kg_datetime_t) { 2028, 2, 29, 0, 0, 0 }; TEST_ASSERT_TRUE(kg_datetime_valid(&t));
  t = (kg_datetime_t) { 2000, 2, 29, 0, 0, 0 }; TEST_ASSERT_TRUE(kg_datetime_valid(&t));
  t = (kg_datetime_t) { 2027, 2, 29, 0, 0, 0 }; TEST_ASSERT_FALSE(kg_datetime_valid(&t));
  t = (kg_datetime_t) { 2026, 4, 31, 0, 0, 0 }; TEST_ASSERT_FALSE(kg_datetime_valid(&t));
  t = (kg_datetime_t) { 2026, 13, 1, 0, 0, 0 }; TEST_ASSERT_FALSE(kg_datetime_valid(&t));
  t = (kg_datetime_t) { 2026, 0, 1, 0, 0, 0 }; TEST_ASSERT_FALSE(kg_datetime_valid(&t));
  t = (kg_datetime_t) { 2026, 1, 0, 0, 0, 0 }; TEST_ASSERT_FALSE(kg_datetime_valid(&t));
  t = (kg_datetime_t) { 2026, 1, 1, 24, 0, 0 }; TEST_ASSERT_FALSE(kg_datetime_valid(&t));
  t = (kg_datetime_t) { 2026, 1, 1, 0, 60, 0 }; TEST_ASSERT_FALSE(kg_datetime_valid(&t));
  t = (kg_datetime_t) { 2026, 1, 1, 0, 0, 60 }; TEST_ASSERT_FALSE(kg_datetime_valid(&t));
  t = (kg_datetime_t) { 1999, 12, 31, 0, 0, 0 }; TEST_ASSERT_FALSE(kg_datetime_valid(&t));
  t = (kg_datetime_t) { 2100, 1, 1, 0, 0, 0 }; TEST_ASSERT_FALSE(kg_datetime_valid(&t));
}

static void test_weekday() {
  kg_datetime_t t = { 2026, 10, 27, 12, 0, 0 }; // Tuesday
  TEST_ASSERT_EQUAL(2, kg_datetime_weekday(&t));
  t = (kg_datetime_t) { 2000, 1, 1, 0, 0, 0 }; TEST_ASSERT_EQUAL(6, kg_datetime_weekday(&t)); // Saturday
  t = (kg_datetime_t) { 2026, 11, 1, 0, 0, 0 }; TEST_ASSERT_EQUAL(0, kg_datetime_weekday(&t)); // Sunday
  t = (kg_datetime_t) { 2028, 2, 29, 0, 0, 0 }; TEST_ASSERT_EQUAL(2, kg_datetime_weekday(&t)); // Tuesday
  t = (kg_datetime_t) { 1970, 1, 1, 0, 0, 0 }; TEST_ASSERT_EQUAL(4, kg_datetime_weekday(&t)); // Thursday
}

static void test_parse_build() {
  kg_datetime_t t = { 1, 1, 1, 1, 1, 1 };
  TEST_ASSERT_TRUE(kg_datetime_parse_build("Oct 27 2026", "17:05:09", &t));
  check(&t, 2026, 10, 27, 17, 5, 9);
  TEST_ASSERT_TRUE(kg_datetime_parse_build("Feb  3 2027", "00:00:00", &t));
  check(&t, 2027, 2, 3, 0, 0, 0);
  TEST_ASSERT_FALSE(kg_datetime_parse_build("Foo  3 2027", "00:00:00", &t));
  TEST_ASSERT_FALSE(kg_datetime_parse_build("Feb 30 2027", "00:00:00", &t));
  TEST_ASSERT_FALSE(kg_datetime_parse_build("Feb  3 2027", "25:00:00", &t));
  TEST_ASSERT_FALSE(kg_datetime_parse_build("Feb  3 27", "00:00:00", &t));
  TEST_ASSERT_FALSE(kg_datetime_parse_build("Feb  3 2027", "0:00:00", &t));
  check(&t, 2027, 2, 3, 0, 0, 0); // left as it was
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse);
  RUN_TEST(test_format);
  RUN_TEST(test_step);
  RUN_TEST(test_add);
  RUN_TEST(test_valid);
  RUN_TEST(test_weekday);
  RUN_TEST(test_parse_build);
  return UNITY_END();
}
