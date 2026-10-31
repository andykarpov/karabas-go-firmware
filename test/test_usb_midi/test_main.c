#include <unity.h>
#include <string.h>
#include "usb_midi.h"

void setUp() {}
void tearDown() {}

static uint8_t conv(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t out[3]) {
  const uint8_t p[4] = { b0, b1, b2, b3 };
  memset(out, 0xAA, 3);
  return kg_usb_midi_packet(p, out);
}

static void test_note_on_off() {
  uint8_t out[3];
  TEST_ASSERT_EQUAL_UINT8(3, conv(0x09, 0x90, 0x3C, 0x64, out));
  const uint8_t on[3] = { 0x90, 0x3C, 0x64 };
  TEST_ASSERT_EQUAL_HEX8_ARRAY(on, out, 3);
  TEST_ASSERT_EQUAL_UINT8(3, conv(0x08, 0x80, 0x3C, 0x00, out));
  TEST_ASSERT_EQUAL_HEX8(0x80, out[0]);
}

static void test_two_byte_messages() {
  uint8_t out[3];
  TEST_ASSERT_EQUAL_UINT8(2, conv(0x0C, 0xC1, 0x05, 0x00, out));
  TEST_ASSERT_EQUAL_HEX8(0xC1, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x05, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0xAA, out[2]); // untouched
  TEST_ASSERT_EQUAL_UINT8(2, conv(0x0D, 0xD0, 0x40, 0x00, out));
  TEST_ASSERT_EQUAL_UINT8(2, conv(0x02, 0xF3, 0x01, 0x00, out)); // song select
}

static void test_three_byte_messages() {
  uint8_t out[3];
  TEST_ASSERT_EQUAL_UINT8(3, conv(0x0B, 0xB0, 0x07, 0x7F, out)); // control change
  TEST_ASSERT_EQUAL_UINT8(3, conv(0x0E, 0xE0, 0x00, 0x40, out)); // pitch bend
  TEST_ASSERT_EQUAL_UINT8(3, conv(0x0A, 0xA0, 0x3C, 0x10, out));
  TEST_ASSERT_EQUAL_UINT8(3, conv(0x03, 0xF2, 0x10, 0x00, out)); // song position
}

static void test_single_bytes() {
  uint8_t out[3];
  TEST_ASSERT_EQUAL_UINT8(1, conv(0x0F, 0xFA, 0x00, 0x00, out)); // start
  TEST_ASSERT_EQUAL_HEX8(0xFA, out[0]);
  TEST_ASSERT_EQUAL_UINT8(1, conv(0x05, 0xF6, 0x00, 0x00, out)); // tune request
  TEST_ASSERT_EQUAL_UINT8(0, conv(0x0F, 0xF8, 0x00, 0x00, out)); // clock dropped
  TEST_ASSERT_EQUAL_UINT8(0, conv(0x0F, 0xFE, 0x00, 0x00, out)); // active sensing dropped
}

static void test_sysex_dropped() {
  uint8_t out[3];
  TEST_ASSERT_EQUAL_UINT8(0, conv(0x04, 0xF0, 0x7E, 0x7F, out));
  TEST_ASSERT_EQUAL_UINT8(0, conv(0x05, 0xF7, 0x00, 0x00, out));
  TEST_ASSERT_EQUAL_UINT8(0, conv(0x06, 0x01, 0xF7, 0x00, out));
  TEST_ASSERT_EQUAL_UINT8(0, conv(0x07, 0x01, 0x02, 0xF7, out));
}

static void test_other_cables_and_padding() {
  uint8_t out[3];
  TEST_ASSERT_EQUAL_UINT8(0, conv(0x19, 0x90, 0x3C, 0x64, out)); // cable 1
  TEST_ASSERT_EQUAL_UINT8(0, conv(0x00, 0x00, 0x00, 0x00, out)); // zero padding
  TEST_ASSERT_EQUAL_UINT8(0, conv(0x01, 0x90, 0x00, 0x00, out)); // reserved CIN
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_note_on_off);
  RUN_TEST(test_two_byte_messages);
  RUN_TEST(test_three_byte_messages);
  RUN_TEST(test_single_bytes);
  RUN_TEST(test_sysex_dropped);
  RUN_TEST(test_other_cables_and_padding);
  return UNITY_END();
}
