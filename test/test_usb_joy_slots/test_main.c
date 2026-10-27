#include <unity.h>
#include "usb_joy_slots.h"

void setUp() {}
void tearDown() {}

static void test_ports_follow_plug_order() {
  usb_joy_slots_t s = {0};
  uint16_t b[2];
  TEST_ASSERT_TRUE(usb_joy_slots_set(&s, 2, 0, true, 1));   // xinput pad first
  TEST_ASSERT_TRUE(usb_joy_slots_set(&s, 1, 3, false, 1));  // then a hid pad
  TEST_ASSERT_TRUE(usb_joy_slots_set(&s, 1, 3, false, 0x41));
  TEST_ASSERT_EQUAL_UINT8(2, usb_joy_slots_snapshot(&s, b));
  TEST_ASSERT_EQUAL_UINT16(1, b[0]);
  TEST_ASSERT_EQUAL_UINT16(0x41, b[1]);
}

static void test_hid_and_xinput_do_not_collide() {
  usb_joy_slots_t s = {0};
  uint16_t b[2];
  usb_joy_slots_set(&s, 1, 0, false, 3);
  usb_joy_slots_set(&s, 1, 0, true, 5);
  usb_joy_slots_snapshot(&s, b);
  TEST_ASSERT_EQUAL_UINT16(3, b[0]);
  TEST_ASSERT_EQUAL_UINT16(5, b[1]);
}

static void test_remove_keeps_other_port() {
  usb_joy_slots_t s = {0};
  uint16_t b[2];
  usb_joy_slots_set(&s, 1, 0, false, 3);
  usb_joy_slots_set(&s, 2, 1, false, 5);
  usb_joy_slots_remove(&s, 1, 0, false);
  TEST_ASSERT_EQUAL_UINT8(2, usb_joy_slots_snapshot(&s, b));
  TEST_ASSERT_EQUAL_UINT16(0, b[0]);
  TEST_ASSERT_EQUAL_UINT16(5, b[1]);
  // a third pad takes the freed port
  TEST_ASSERT_TRUE(usb_joy_slots_set(&s, 3, 2, true, 7));
  usb_joy_slots_snapshot(&s, b);
  TEST_ASSERT_EQUAL_UINT16(7, b[0]);
}

static void test_full() {
  usb_joy_slots_t s = {0};
  usb_joy_slots_set(&s, 1, 0, false, 1);
  usb_joy_slots_set(&s, 2, 0, false, 1);
  TEST_ASSERT_FALSE(usb_joy_slots_set(&s, 3, 0, false, 1));
  TEST_ASSERT_FALSE(usb_joy_slots_set(&s, 0, 0, false, 1)); // address 0 is never a device
}

static void test_get_port() {
  usb_joy_slots_t s = {0};
  uint8_t a, i;
  bool x;
  usb_joy_slots_set(&s, 4, 1, true, 1);
  TEST_ASSERT_TRUE(usb_joy_slots_get(&s, 0, &a, &i, &x));
  TEST_ASSERT_EQUAL_UINT8(4, a);
  TEST_ASSERT_EQUAL_UINT8(1, i);
  TEST_ASSERT_TRUE(x);
  TEST_ASSERT_FALSE(usb_joy_slots_get(&s, 1, &a, &i, &x));
  TEST_ASSERT_FALSE(usb_joy_slots_get(&s, 2, &a, &i, &x));
  usb_joy_slots_remove(&s, 4, 1, true);
  TEST_ASSERT_FALSE(usb_joy_slots_get(&s, 0, &a, &i, &x));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ports_follow_plug_order);
  RUN_TEST(test_hid_and_xinput_do_not_collide);
  RUN_TEST(test_remove_keeps_other_port);
  RUN_TEST(test_full);
  RUN_TEST(test_get_port);
  return UNITY_END();
}
