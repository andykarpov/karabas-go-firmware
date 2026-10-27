#include <unity.h>
#include "rtc_regs.h"

void setUp() {}
void tearDown() {}

static void test_bcd() {
  TEST_ASSERT_EQUAL_HEX8(0x00, kg_bin2bcd(0));
  TEST_ASSERT_EQUAL_HEX8(0x59, kg_bin2bcd(59));
  TEST_ASSERT_EQUAL_HEX8(0x99, kg_bin2bcd(99));
  TEST_ASSERT_EQUAL(0, kg_bcd2bin(0x00));
  TEST_ASSERT_EQUAL(59, kg_bcd2bin(0x59));
  TEST_ASSERT_EQUAL(99, kg_bcd2bin(0x99));
}

static void test_encode() {
  kg_datetime_t t = { 2026, 10, 27, 17, 5, 9 }; // a Tuesday
  uint8_t r[KG_DS3231_TIME_REGS];
  kg_ds3231_encode(&t, r);
  const uint8_t want[] = { 0x09, 0x05, 0x17, 0x02, 0x27, 0x10, 0x26 };
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, r, KG_DS3231_TIME_REGS);
  t = (kg_datetime_t) { 2026, 11, 1, 0, 0, 0 }; // a Sunday: 7
  kg_ds3231_encode(&t, r);
  TEST_ASSERT_EQUAL_HEX8(0x07, r[3]);
}

static void test_decode() {
  kg_datetime_t t = { 0, 0, 0, 0, 0, 0 };
  const uint8_t r[] = { 0x09, 0x05, 0x17, 0x02, 0x27, 0x10, 0x26 };
  TEST_ASSERT_TRUE(kg_ds3231_decode(r, &t));
  TEST_ASSERT_EQUAL(2026, t.year); TEST_ASSERT_EQUAL(10, t.month); TEST_ASSERT_EQUAL(27, t.day);
  TEST_ASSERT_EQUAL(17, t.hour); TEST_ASSERT_EQUAL(5, t.minute); TEST_ASSERT_EQUAL(9, t.second);
  // the century bit and a wrong day of the week do not matter
  const uint8_t c[] = { 0x00, 0x00, 0x00, 0x05, 0x01, 0x81, 0x27 };
  TEST_ASSERT_TRUE(kg_ds3231_decode(c, &t));
  TEST_ASSERT_EQUAL(2027, t.year); TEST_ASSERT_EQUAL(1, t.month);
  // a failed read (all FF), 12 h mode, a date that does not exist
  const uint8_t ff[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  const uint8_t h12[] = { 0x00, 0x00, 0x52, 0x01, 0x01, 0x01, 0x26 };
  const uint8_t feb30[] = { 0x00, 0x00, 0x00, 0x01, 0x30, 0x02, 0x26 };
  t.year = 1;
  TEST_ASSERT_FALSE(kg_ds3231_decode(ff, &t));
  TEST_ASSERT_FALSE(kg_ds3231_decode(h12, &t));
  TEST_ASSERT_FALSE(kg_ds3231_decode(feb30, &t));
  TEST_ASSERT_EQUAL(1, t.year); // left as it was
}

static void test_round_trip() {
  kg_datetime_t t = { 2099, 12, 31, 23, 59, 59 }, u;
  uint8_t r[KG_DS3231_TIME_REGS];
  kg_ds3231_encode(&t, r);
  TEST_ASSERT_TRUE(kg_ds3231_decode(r, &u));
  TEST_ASSERT_EQUAL(2099, u.year); TEST_ASSERT_EQUAL(12, u.month); TEST_ASSERT_EQUAL(31, u.day);
  TEST_ASSERT_EQUAL(23, u.hour); TEST_ASSERT_EQUAL(59, u.minute); TEST_ASSERT_EQUAL(59, u.second);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_bcd);
  RUN_TEST(test_encode);
  RUN_TEST(test_decode);
  RUN_TEST(test_round_trip);
  return UNITY_END();
}
