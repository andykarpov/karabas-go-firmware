#include <unity.h>
#include "kg_format.h"

void setUp() {}
void tearDown() {}

static void test_be_readers() {
  const uint8_t buf[] = {0x12, 0x34, 0x56, 0x78};
  TEST_ASSERT_EQUAL_HEX16(0x1234, kg_be16(buf));
  TEST_ASSERT_EQUAL_HEX32(0x123456, kg_be24(buf));
  TEST_ASSERT_EQUAL_HEX32(0x12345678, kg_be32(buf));
}

static void test_be32_high_bit() {
  const uint8_t buf[] = {0xFF, 0x00, 0x00, 0x01};
  TEST_ASSERT_EQUAL_HEX32(0xFF000001, kg_be32(buf));
}

static void test_rom_block_internal() {
  // len = 0x4000, addr = 0x00010000
  const uint8_t hdr[] = {0x00, 0x00, 0x40, 0x00, 0x00, 0x01, 0x00, 0x00};
  kg_rom_block_t rom;
  TEST_ASSERT_TRUE(kg_rom_block_parse(hdr, 0x4000 + 8, &rom));
  TEST_ASSERT_FALSE(rom.external);
  TEST_ASSERT_EQUAL_UINT32(0x4000, rom.len);
  TEST_ASSERT_EQUAL_HEX32(0x00010000, rom.addr);
}

static void test_rom_block_external() {
  // make_core writes len(filename) | 1<<31 for external roms
  const uint8_t hdr[] = {0x80, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00};
  kg_rom_block_t rom;
  TEST_ASSERT_TRUE(kg_rom_block_parse(hdr, 12 + 8, &rom));
  TEST_ASSERT_TRUE(rom.external);
  TEST_ASSERT_EQUAL_UINT32(12, rom.len);
}

static void test_rom_block_out_of_bounds() {
  const uint8_t hdr[] = {0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00};
  kg_rom_block_t rom;
  TEST_ASSERT_FALSE(kg_rom_block_parse(hdr, 0x4000 + 7, &rom));
  TEST_ASSERT_FALSE(kg_rom_block_parse(hdr, 4, &rom));
  TEST_ASSERT_FALSE(kg_rom_block_parse(hdr, 0, &rom));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_be_readers);
  RUN_TEST(test_be32_high_bit);
  RUN_TEST(test_rom_block_internal);
  RUN_TEST(test_rom_block_external);
  RUN_TEST(test_rom_block_out_of_bounds);
  return UNITY_END();
}
