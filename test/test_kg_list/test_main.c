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

static void test_pages() {
  TEST_ASSERT_EQUAL_UINT16(1, kg_list_pages(0, 16));
  TEST_ASSERT_EQUAL_UINT16(1, kg_list_pages(16, 16));
  TEST_ASSERT_EQUAL_UINT16(2, kg_list_pages(17, 16));
  TEST_ASSERT_EQUAL_UINT16(1, kg_list_page(0, 16));
  TEST_ASSERT_EQUAL_UINT16(1, kg_list_page(15, 16));
  TEST_ASSERT_EQUAL_UINT16(2, kg_list_page(16, 16));
  TEST_ASSERT_EQUAL_UINT16(5, kg_list_page(4, 0)); // page 0 is taken as 1
}

static void test_window() {
  TEST_ASSERT_EQUAL_UINT16(0, kg_list_window(5, 3, 16, 10)); // fits: always from 0
  TEST_ASSERT_EQUAL_UINT16(0, kg_list_window(0, 15, 16, 40));
  TEST_ASSERT_EQUAL_UINT16(1, kg_list_window(0, 16, 16, 40)); // one row down
  TEST_ASSERT_EQUAL_UINT16(10, kg_list_window(12, 10, 16, 40)); // up to the selection
  TEST_ASSERT_EQUAL_UINT16(12, kg_list_window(12, 20, 16, 40)); // in view: stays
  TEST_ASSERT_EQUAL_UINT16(24, kg_list_window(0, 39, 16, 40)); // around the end
  TEST_ASSERT_EQUAL_UINT16(24, kg_list_window(30, 30, 16, 40)); // past the end: clamped
}

static void thumb(uint16_t h, uint16_t top, uint16_t vis, uint16_t total, uint16_t pos, uint16_t len) {
  uint16_t p = 99, l = 99;
  TEST_ASSERT_TRUE(kg_list_thumb(h, top, vis, total, &p, &l));
  TEST_ASSERT_EQUAL_UINT16(pos, p);
  TEST_ASSERT_EQUAL_UINT16(len, l);
}

static void test_thumb() {
  uint16_t p, l;
  TEST_ASSERT_FALSE(kg_list_thumb(16, 0, 16, 16, &p, &l)); // fits
  TEST_ASSERT_FALSE(kg_list_thumb(1, 0, 1, 5, &p, &l));    // no room
  // 32 items, 16 shown: half the track
  thumb(16, 0, 16, 32, 0, 8);
  thumb(16, 16, 16, 32, 8, 8);
  thumb(16, 1, 16, 32, 1, 8);  // one row down: off the top
  thumb(16, 15, 16, 32, 7, 8); // one row up from the end: off the bottom
  // pages of 16 of 200 items: one cell, the last page (top past total - visible) at the bottom
  thumb(16, 0, 16, 200, 0, 1);
  thumb(16, 16, 16, 200, 1, 1);
  thumb(16, 96, 16, 200, 8, 1);
  thumb(16, 192, 16, 200, 15, 1);
  // 17 of 16: the longest thumb is height - 1
  thumb(16, 0, 16, 17, 0, 15);
  thumb(16, 1, 16, 17, 1, 15);
  // 2 positions: room 1
  thumb(2, 0, 1, 2, 0, 1);
  thumb(2, 1, 1, 2, 1, 1);
}

// a core menu: text rows (inactive) between the options
static bool active_odd(uint16_t i, void *ctx) { (void) ctx; return i % 2 == 1; }
static bool active_none(uint16_t i, void *ctx) { (void) i; (void) ctx; return false; }

static void test_active() {
  TEST_ASSERT_EQUAL_UINT16(1, kg_list_first_active(6, active_odd, NULL));
  TEST_ASSERT_EQUAL_UINT16(6, kg_list_first_active(6, active_none, NULL));
  TEST_ASSERT_EQUAL_UINT16(3, kg_list_step_active(1, 6, 1, active_odd, NULL));
  TEST_ASSERT_EQUAL_UINT16(1, kg_list_step_active(5, 6, 1, active_odd, NULL));  // around the end
  TEST_ASSERT_EQUAL_UINT16(5, kg_list_step_active(1, 6, -1, active_odd, NULL)); // around the start
  TEST_ASSERT_EQUAL_UINT16(3, kg_list_step_active(5, 6, -1, active_odd, NULL));
  TEST_ASSERT_EQUAL_UINT16(1, kg_list_step_active(1, 2, 1, active_odd, NULL));  // the only one
  TEST_ASSERT_EQUAL_UINT16(2, kg_list_step_active(2, 6, 1, active_none, NULL)); // none: stays
  TEST_ASSERT_EQUAL_UINT16(0, kg_list_step_active(0, 0, 1, active_odd, NULL));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_up_down_wrap);
  RUN_TEST(test_pages_stop_at_the_ends);
  RUN_TEST(test_empty_and_one);
  RUN_TEST(test_selection_past_the_end);
  RUN_TEST(test_moved_flag);
  RUN_TEST(test_pages);
  RUN_TEST(test_window);
  RUN_TEST(test_thumb);
  RUN_TEST(test_active);
  return UNITY_END();
}
