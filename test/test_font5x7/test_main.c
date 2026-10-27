#include <unity.h>
#include "font5x7.h"

void setUp() {}
void tearDown() {}

// "A" of glcdfont: 0x7C, 0x12, 0x11, 0x12, 0x7C
static void test_column() {
  TEST_ASSERT_EQUAL_HEX8(0x7C, kg_font_column('A', 0));
  TEST_ASSERT_EQUAL_HEX8(0x11, kg_font_column('A', 2));
  TEST_ASSERT_EQUAL_HEX8(0x00, kg_font_column('A', 5)); // the gap
  TEST_ASSERT_EQUAL_HEX8(0x00, kg_font_column(' ', 0));
}

static void test_pixels() {
  // A at 0: column 0 is 0x7C, rows 2..6
  TEST_ASSERT_FALSE(kg_text_pixel("A", 0, 0, 1));
  TEST_ASSERT_TRUE(kg_text_pixel("A", 0, 0, 2));
  TEST_ASSERT_TRUE(kg_text_pixel("A", 0, 0, 6));
  TEST_ASSERT_FALSE(kg_text_pixel("A", 0, 0, 7));
  TEST_ASSERT_TRUE(kg_text_pixel("A", 0, 2, 0));  // the top of the A
  TEST_ASSERT_FALSE(kg_text_pixel("A", 0, 5, 2)); // the gap column
  // the second character starts 6 pixels on; pos moves the text
  TEST_ASSERT_TRUE(kg_text_pixel("AA", 0, 6, 2));
  TEST_ASSERT_TRUE(kg_text_pixel("A", 3, 3, 2));
  TEST_ASSERT_FALSE(kg_text_pixel("A", 3, 2, 2));
  // scrolled in from the left: x 0 is column 2 of the A
  TEST_ASSERT_TRUE(kg_text_pixel("A", -2, 0, 0));
  // past the end, off the rows
  TEST_ASSERT_FALSE(kg_text_pixel("A", 0, 6, 2));
  TEST_ASSERT_FALSE(kg_text_pixel("A", 0, 0, 8));
  TEST_ASSERT_FALSE(kg_text_pixel("A", 0, 0, -1));
  TEST_ASSERT_FALSE(kg_text_pixel("", 0, 0, 2));
}

static void test_classic_charset() {
  // GFX without cp437 draws code 176 with the glyph of 177
  const char s[] = { (char) 176, 0 };
  for (int col = 0; col < 5; col++) {
    uint8_t g = kg_font_column(177, col);
    for (int y = 0; y < 8; y++) {
      TEST_ASSERT_EQUAL((g >> y) & 1, kg_text_pixel(s, 0, col, y));
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_column);
  RUN_TEST(test_pixels);
  RUN_TEST(test_classic_charset);
  return UNITY_END();
}
