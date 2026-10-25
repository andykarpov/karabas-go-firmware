#include <unity.h>
#include "usb_joy_slots.h"

void setUp() {}
void tearDown() {}

static void test_ports_follow_plug_order() {
  UsbJoySlots<2> s;
  uint16_t b[2];
  TEST_ASSERT_TRUE(s.set(2, 0, true, 1));   // xinput pad first
  TEST_ASSERT_TRUE(s.set(1, 3, false, 1));  // then a hid pad
  TEST_ASSERT_TRUE(s.set(1, 3, false, 0x41));
  TEST_ASSERT_EQUAL_UINT8(2, s.snapshot(b));
  TEST_ASSERT_EQUAL_UINT16(1, b[0]);
  TEST_ASSERT_EQUAL_UINT16(0x41, b[1]);
}

static void test_hid_and_xinput_do_not_collide() {
  UsbJoySlots<2> s;
  uint16_t b[2];
  s.set(1, 0, false, 3);
  s.set(1, 0, true, 5);
  s.snapshot(b);
  TEST_ASSERT_EQUAL_UINT16(3, b[0]);
  TEST_ASSERT_EQUAL_UINT16(5, b[1]);
}

static void test_remove_keeps_other_port() {
  UsbJoySlots<2> s;
  uint16_t b[2];
  s.set(1, 0, false, 3);
  s.set(2, 1, false, 5);
  s.remove(1, 0, false);
  TEST_ASSERT_EQUAL_UINT8(2, s.snapshot(b));
  TEST_ASSERT_EQUAL_UINT16(0, b[0]);
  TEST_ASSERT_EQUAL_UINT16(5, b[1]);
  // a third pad takes the freed port
  TEST_ASSERT_TRUE(s.set(3, 2, true, 7));
  s.snapshot(b);
  TEST_ASSERT_EQUAL_UINT16(7, b[0]);
}

static void test_full() {
  UsbJoySlots<2> s;
  s.set(1, 0, false, 1);
  s.set(2, 0, false, 1);
  TEST_ASSERT_FALSE(s.set(3, 0, false, 1));
  TEST_ASSERT_FALSE(s.set(0, 0, false, 1)); // address 0 is never a device
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ports_follow_plug_order);
  RUN_TEST(test_hid_and_xinput_do_not_collide);
  RUN_TEST(test_remove_keeps_other_port);
  RUN_TEST(test_full);
  return UNITY_END();
}
