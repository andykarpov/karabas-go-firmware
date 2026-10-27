#include <unity.h>
#include <string.h>
#include "crc32.h"

void setUp() {}
void tearDown() {}

static void test_check_value() {
  // the standard check value of CRC-32/ISO-HDLC
  const char *s = "123456789";
  uint32_t crc = kg_crc32_update(KG_CRC32_INIT, (const uint8_t *) s, strlen(s));
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, kg_crc32_final(crc));
}

static void test_empty() {
  TEST_ASSERT_EQUAL_HEX32(0x00000000u, kg_crc32_final(KG_CRC32_INIT));
}

static void test_chunked_equals_whole() {
  uint8_t buf[1000];
  for (int i = 0; i < 1000; i++) buf[i] = (uint8_t) (i * 7 + 3);
  uint32_t whole = kg_crc32_final(kg_crc32_update(KG_CRC32_INIT, buf, sizeof(buf)));
  uint32_t crc = KG_CRC32_INIT;
  crc = kg_crc32_update(crc, buf, 512);
  crc = kg_crc32_update(crc, buf + 512, 1);
  crc = kg_crc32_update(crc, buf + 513, sizeof(buf) - 513);
  TEST_ASSERT_EQUAL_HEX32(whole, kg_crc32_final(crc));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_check_value);
  RUN_TEST(test_empty);
  RUN_TEST(test_chunked_equals_whole);
  return UNITY_END();
}
