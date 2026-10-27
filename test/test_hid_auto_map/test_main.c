#include <unity.h>
#include <string.h>
#include "hid_auto_map.h"

void setUp() {}
void tearDown() {}

// DragonRise generic USB pad (0079:0006), the layout of the Retrolink / SNES style pads:
// 5 axis bytes (X Y X X Y), a 4-bit null field, 10 buttons, 10 vendor bits, no report id.
static const uint8_t dragonrise[] = {
  0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0xa1, 0x02, 0x75, 0x08, 0x95, 0x05, 0x15, 0x00, 0x26, 0xff,
  0x00, 0x35, 0x00, 0x46, 0xff, 0x00, 0x09, 0x30, 0x09, 0x31, 0x09, 0x30, 0x09, 0x30, 0x09, 0x31,
  0x81, 0x02, 0x75, 0x04, 0x95, 0x01, 0x25, 0x07, 0x46, 0x3b, 0x01, 0x65, 0x14, 0x09, 0x00, 0x81,
  0x42, 0x65, 0x00, 0x75, 0x01, 0x95, 0x0a, 0x25, 0x01, 0x45, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29,
  0x0a, 0x81, 0x02, 0x06, 0x00, 0xff, 0x75, 0x01, 0x95, 0x0a, 0x25, 0x01, 0x45, 0x01, 0x09, 0x01,
  0x81, 0x02, 0xc0, 0xa1, 0x02, 0x75, 0x08, 0x95, 0x04, 0x46, 0xff, 0x00, 0x26, 0xff, 0x00, 0x09,
  0x02, 0x91, 0x02, 0xc0, 0xc0
};

// Gamepad with report id 1: 16 buttons, a hat (0..7, null state), 4 padding bits,
// signed X / Y bytes. Then report id 2 with one button, to check that ids are kept apart.
static const uint8_t hat_pad[] = {
  0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x85, 0x01,
  0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02,
  0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3b, 0x01, 0x65, 0x14,
  0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
  0x65, 0x00, 0x75, 0x04, 0x95, 0x01, 0x81, 0x03,
  0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
  0x85, 0x02, 0x05, 0x09, 0x09, 0x01, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
  0x75, 0x07, 0x81, 0x03,
  0xc0
};

static const uint8_t keyboard[] = {
  0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
  0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0xc0
};

static hid_auto_map_t m;

static void test_dragonrise_rest() {
  TEST_ASSERT_TRUE(hid_auto_map_build(dragonrise, sizeof(dragonrise), &m));
  const uint8_t r[8] = {0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x0f, 0x00, 0x00};
  TEST_ASSERT_EQUAL_UINT16(HID_SC_CTL_ON, hid_auto_map_report(&m, 0, r, sizeof(r), HID_SC_CTL_ON));
}

// same bits as drivers/retrolink.ini
static void test_dragonrise_matches_retrolink_ini() {
  hid_auto_map_build(dragonrise, sizeof(dragonrise), &m);
  typedef struct { uint8_t byte, value; uint16_t btn; } value_case_t;
  const value_case_t cases[] = {
    {1, 0x00, HID_SC_UP}, {1, 0xff, HID_SC_DOWN}, {0, 0x00, HID_SC_LEFT}, {0, 0xff, HID_SC_RIGHT},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    const value_case_t *c = &cases[i];
    uint8_t r[8] = {0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x0f, 0x00, 0x00};
    r[c->byte] = c->value;
    TEST_ASSERT_EQUAL_UINT16(c->btn, hid_auto_map_report(&m, 0, r, sizeof(r), 0));
  }
  typedef struct { uint8_t byte, bit; uint16_t btn; } bit_case_t;
  const bit_case_t bits[] = {
    {5, 5, HID_SC_A}, {5, 6, HID_SC_B}, {5, 4, HID_SC_X}, {5, 7, HID_SC_Y},
    {6, 5, HID_SC_START}, {6, 4, HID_SC_MODE}, {6, 0, HID_SC_C}, {6, 1, HID_SC_Z},
  };
  for (size_t i = 0; i < sizeof(bits) / sizeof(bits[0]); i++) {
    const bit_case_t *c = &bits[i];
    uint8_t r[8] = {0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x0f, 0x00, 0x00};
    r[c->byte] |= 1 << c->bit;
    TEST_ASSERT_EQUAL_UINT16(c->btn, hid_auto_map_report(&m, 0, r, sizeof(r), 0));
  }
}

static void test_dragonrise_short_report() {
  hid_auto_map_build(dragonrise, sizeof(dragonrise), &m);
  const uint8_t r[2] = {0x00, 0x00};
  // only the first X and Y fit
  TEST_ASSERT_EQUAL_UINT16(HID_SC_LEFT | HID_SC_UP, hid_auto_map_report(&m, 0, r, sizeof(r), 0));
}

static void test_hat() {
  TEST_ASSERT_TRUE(hid_auto_map_build(hat_pad, sizeof(hat_pad), &m));
  uint8_t r[5] = {0, 0, 0x08, 0, 0}; // hat null
  TEST_ASSERT_EQUAL_UINT16(0, hid_auto_map_report(&m, 1, r, sizeof(r), 0));
  const uint16_t dirs[8] = {
    HID_SC_UP, HID_SC_UP | HID_SC_RIGHT, HID_SC_RIGHT, HID_SC_DOWN | HID_SC_RIGHT,
    HID_SC_DOWN, HID_SC_DOWN | HID_SC_LEFT, HID_SC_LEFT, HID_SC_UP | HID_SC_LEFT
  };
  for (uint8_t i = 0; i < 8; i++) {
    r[2] = 0xf0 | i; // padding bits set, must be ignored
    TEST_ASSERT_EQUAL_UINT16(dirs[i], hid_auto_map_report(&m, 1, r, sizeof(r), 0));
  }
}

static void test_signed_axes_and_buttons() {
  hid_auto_map_build(hat_pad, sizeof(hat_pad), &m);
  uint8_t r[5] = {0, 0, 0x08, 0x80, 0x7f}; // X -128, Y 127
  TEST_ASSERT_EQUAL_UINT16(HID_SC_LEFT | HID_SC_DOWN, hid_auto_map_report(&m, 1, r, sizeof(r), 0));
  r[3] = 0x20; r[4] = 0xe0; // inside the dead zone
  TEST_ASSERT_EQUAL_UINT16(0, hid_auto_map_report(&m, 1, r, sizeof(r), 0));
  r[3] = 0; r[4] = 0;
  r[0] = 0x02; r[1] = 0x02; // button 2 -> A, button 10 -> Start
  TEST_ASSERT_EQUAL_UINT16(HID_SC_A | HID_SC_START, hid_auto_map_report(&m, 1, r, sizeof(r), 0));
  r[1] = 0x80; // button 16 is not mapped
  r[0] = 0;
  TEST_ASSERT_EQUAL_UINT16(0, hid_auto_map_report(&m, 1, r, sizeof(r), 0));
}

static void test_report_ids_kept_apart() {
  hid_auto_map_build(hat_pad, sizeof(hat_pad), &m);
  const uint8_t r2[1] = {0x01}; // report 2, button 1
  TEST_ASSERT_EQUAL_UINT16(HID_SC_X, hid_auto_map_report(&m, 2, r2, sizeof(r2), 0));
  // button 1 of report 1 is X too, an unknown report id maps nothing
  const uint8_t r1[5] = {0x01, 0, 0x08, 0, 0};
  TEST_ASSERT_EQUAL_UINT16(HID_SC_X, hid_auto_map_report(&m, 1, r1, sizeof(r1), 0));
  TEST_ASSERT_EQUAL_UINT16(0, hid_auto_map_report(&m, 3, r1, sizeof(r1), 0));
  TEST_ASSERT_TRUE(hid_auto_map_has_report(&m, 1));
  TEST_ASSERT_TRUE(hid_auto_map_has_report(&m, 2));
  TEST_ASSERT_FALSE(hid_auto_map_has_report(&m, 3));
}

static void test_keyboard_not_mapped() {
  TEST_ASSERT_FALSE(hid_auto_map_build(keyboard, sizeof(keyboard), &m));
  TEST_ASSERT_EQUAL_UINT8(0, m.count);
}

static void test_truncated_descriptor() {
  // must not read past the end
  for (uint16_t len = 0; len < sizeof(hat_pad); len++) hid_auto_map_build(hat_pad, len, &m);
  const uint8_t long_item[] = {0xfe, 0x40, 0x00};
  TEST_ASSERT_FALSE(hid_auto_map_build(long_item, sizeof(long_item), &m));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dragonrise_rest);
  RUN_TEST(test_dragonrise_matches_retrolink_ini);
  RUN_TEST(test_dragonrise_short_report);
  RUN_TEST(test_hat);
  RUN_TEST(test_signed_axes_and_buttons);
  RUN_TEST(test_report_ids_kept_apart);
  RUN_TEST(test_keyboard_not_mapped);
  RUN_TEST(test_truncated_descriptor);
  return UNITY_END();
}
