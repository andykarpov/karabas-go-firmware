#include <unity.h>
#include "kg_ring.h"

void setUp() {}
void tearDown() {}

static void test_put_get() {
  uint8_t buf[4];
  kg_ring_t r;
  kg_ring_init(&r, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_INT(-1, kg_ring_get(&r));
  TEST_ASSERT_EQUAL_UINT32(4, kg_ring_free(&r));
  TEST_ASSERT_TRUE(kg_ring_put(&r, 1));
  TEST_ASSERT_TRUE(kg_ring_put(&r, 2));
  TEST_ASSERT_EQUAL_UINT32(2, kg_ring_count(&r));
  TEST_ASSERT_EQUAL_INT(1, kg_ring_peek(&r));
  TEST_ASSERT_EQUAL_INT(1, kg_ring_get(&r));
  TEST_ASSERT_EQUAL_INT(2, kg_ring_get(&r));
  TEST_ASSERT_EQUAL_INT(-1, kg_ring_get(&r));
}

static void test_full_drops_and_wraps() {
  uint8_t buf[3];
  kg_ring_t r;
  kg_ring_init(&r, buf, sizeof(buf));
  for (int round = 0; round < 5; round++) {
    TEST_ASSERT_TRUE(kg_ring_put(&r, 10));
    TEST_ASSERT_TRUE(kg_ring_put(&r, 11));
    TEST_ASSERT_TRUE(kg_ring_put(&r, 0xff));
    TEST_ASSERT_FALSE(kg_ring_put(&r, 13)); // full: dropped
    TEST_ASSERT_EQUAL_UINT32(0, kg_ring_free(&r));
    TEST_ASSERT_EQUAL_INT(10, kg_ring_get(&r));
    TEST_ASSERT_EQUAL_INT(11, kg_ring_get(&r));
    TEST_ASSERT_EQUAL_INT(0xff, kg_ring_get(&r));
  }
  kg_ring_put(&r, 1);
  kg_ring_clear(&r);
  TEST_ASSERT_EQUAL_UINT32(0, kg_ring_count(&r));
  TEST_ASSERT_EQUAL_INT(-1, kg_ring_peek(&r));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_put_get);
  RUN_TEST(test_full_drops_and_wraps);
  return UNITY_END();
}
