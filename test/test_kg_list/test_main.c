#include <unity.h>
#include "kg_list.h"

void setUp() {}
void tearDown() {}

static uint16_t moved(uint16_t sel, uint16_t len, uint16_t page, kg_list_move_t m) {
  kg_list_move(&sel, len, page, m);
  return sel;
}

static void test_up_down_wrap() {
  TEST_ASSERT_EQUAL_UINT16(1, moved(0, 5, 3, KG_LIST_DOWN));
  TEST_ASSERT_EQUAL_UINT16(0, moved(4, 5, 3, KG_LIST_DOWN));
  TEST_ASSERT_EQUAL_UINT16(4, moved(0, 5, 3, KG_LIST_UP));
  TEST_ASSERT_EQUAL_UINT16(2, moved(3, 5, 3, KG_LIST_UP));
}

static void test_pages_stop_at_the_ends() {
  TEST_ASSERT_EQUAL_UINT16(3, moved(0, 10, 3, KG_LIST_PAGE_DOWN));
  TEST_ASSERT_EQUAL_UINT16(9, moved(8, 10, 3, KG_LIST_PAGE_DOWN));
  TEST_ASSERT_EQUAL_UINT16(9, moved(6, 10, 3, KG_LIST_PAGE_DOWN));
  TEST_ASSERT_EQUAL_UINT16(4, moved(7, 10, 3, KG_LIST_PAGE_UP));
  TEST_ASSERT_EQUAL_UINT16(0, moved(2, 10, 3, KG_LIST_PAGE_UP));
  TEST_ASSERT_EQUAL_UINT16(0, moved(3, 10, 3, KG_LIST_PAGE_UP));
}

static void test_empty_and_one() {
  uint16_t sel = 0;
  TEST_ASSERT_FALSE(kg_list_move(&sel, 0, 3, KG_LIST_UP));
  TEST_ASSERT_EQUAL_UINT16(0, sel);
  TEST_ASSERT_FALSE(kg_list_move(&sel, 1, 3, KG_LIST_DOWN));
  TEST_ASSERT_FALSE(kg_list_move(&sel, 1, 3, KG_LIST_PAGE_UP));
  TEST_ASSERT_EQUAL_UINT16(0, sel);
}

static void test_selection_past_the_end() {
  // the list got shorter: the selection comes back onto it
  TEST_ASSERT_EQUAL_UINT16(3, moved(9, 5, 3, KG_LIST_UP));
  TEST_ASSERT_EQUAL_UINT16(0, moved(9, 5, 3, KG_LIST_DOWN));
}

static void test_moved_flag() {
  uint16_t sel = 4;
  TEST_ASSERT_FALSE(kg_list_move(&sel, 5, 3, KG_LIST_PAGE_DOWN));
  TEST_ASSERT_TRUE(kg_list_move(&sel, 5, 3, KG_LIST_PAGE_UP));
  TEST_ASSERT_EQUAL_UINT16(1, sel);
  TEST_ASSERT_TRUE(kg_list_move(&sel, 5, 0, KG_LIST_PAGE_DOWN)); // page 0 is taken as 1
  TEST_ASSERT_EQUAL_UINT16(2, sel);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_up_down_wrap);
  RUN_TEST(test_pages_stop_at_the_ends);
  RUN_TEST(test_empty_and_one);
  RUN_TEST(test_selection_past_the_end);
  RUN_TEST(test_moved_flag);
  return UNITY_END();
}
