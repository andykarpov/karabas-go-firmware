#include <string.h>
#include <unity.h>
#include "matrix_fx.h"

void setUp() {}
void tearDown() {}

static uint8_t count(const matrix_fx_t *fx, uint8_t color) {
  uint8_t n = 0;
  for (uint8_t y = 0; y < KG_MATRIX_H; y++)
    for (uint8_t x = 0; x < KG_MATRIX_W; x++)
      if (fx->fb[y][x] == color) n++;
  return n;
}

static void test_life_blinker() {
  uint16_t cells[KG_MATRIX_H] = {0}, next[KG_MATRIX_H], back[KG_MATRIX_H];
  cells[3] = 0x7 << 4; // horizontal blinker at x 4..6
  matrix_fx_life_next(cells, next);
  TEST_ASSERT_EQUAL_HEX16(1 << 5, next[2]);
  TEST_ASSERT_EQUAL_HEX16(1 << 5, next[3]);
  TEST_ASSERT_EQUAL_HEX16(1 << 5, next[4]);
  TEST_ASSERT_EQUAL_HEX16(0, next[1]);
  matrix_fx_life_next(next, back);
  TEST_ASSERT_EQUAL_MEMORY(cells, back, sizeof(cells));
}

static void test_life_wraps() {
  // a block across the corner of the torus stays as it is
  uint16_t cells[KG_MATRIX_H] = {0}, next[KG_MATRIX_H];
  cells[0] = 0x8001;
  cells[7] = 0x8001;
  matrix_fx_life_next(cells, next);
  TEST_ASSERT_EQUAL_MEMORY(cells, next, sizeof(cells));
}

static void test_life_reseeds() {
  matrix_fx_t fx;
  matrix_fx_init(&fx, KG_MATRIX_FX_LIFE, 0, 1234);
  for (int i = 0; i < 2000; i++) matrix_fx_step(&fx, nullptr);
  // still something alive (dead fields are seeded again)
  uint16_t any = 0;
  for (uint8_t y = 0; y < KG_MATRIX_H; y++) any |= fx.cells[y];
  TEST_ASSERT_NOT_EQUAL(0, any);
}

static void test_clock() {
  matrix_fx_t fx;
  matrix_fx_init(&fx, KG_MATRIX_FX_CLOCK, 0, 1);
  matrix_fx_time_t t = {12, 34, 59};
  matrix_fx_step(&fx, &t);
  // "1" top row: only the middle column
  TEST_ASSERT_EQUAL_UINT8(KG_MX_GREEN, fx.fb[1][1]);
  TEST_ASSERT_EQUAL_UINT8(KG_MX_OFF, fx.fb[1][0]);
  // "4" of the minutes: x 13..15, top row 1 0 1
  TEST_ASSERT_EQUAL_UINT8(KG_MX_YELLOW, fx.fb[1][13]);
  TEST_ASSERT_EQUAL_UINT8(KG_MX_OFF, fx.fb[1][14]);
  TEST_ASSERT_EQUAL_UINT8(KG_MX_YELLOW, fx.fb[1][15]);
  // the seconds dot
  TEST_ASSERT_EQUAL_UINT8(KG_MX_RED, fx.fb[7][15]);
  TEST_ASSERT_EQUAL_UINT8(1, count(&fx, KG_MX_RED));
  t.second = 0;
  matrix_fx_step(&fx, &t);
  TEST_ASSERT_EQUAL_UINT8(KG_MX_RED, fx.fb[7][0]);
  TEST_ASSERT_EQUAL_UINT8(1, count(&fx, KG_MX_RED));
}

static void test_scanner_bounces() {
  matrix_fx_t fx;
  matrix_fx_init(&fx, KG_MATRIX_FX_SCANNER, 0, 1);
  for (int i = 0; i < 100; i++) {
    matrix_fx_step(&fx, nullptr);
    TEST_ASSERT_TRUE(fx.pos >= 0 && fx.pos < KG_MATRIX_W);
    TEST_ASSERT_TRUE(count(&fx, KG_MX_RED) >= 4); // the head
    TEST_ASSERT_EQUAL_UINT8(0, count(&fx, KG_MX_GREEN));
  }
  matrix_fx_init(&fx, KG_MATRIX_FX_SCANNER, KG_MX_GREEN, 1);
  matrix_fx_step(&fx, nullptr);
  TEST_ASSERT_EQUAL_UINT8(4, count(&fx, KG_MX_GREEN));
}

static void test_fire_rises() {
  matrix_fx_t fx;
  matrix_fx_init(&fx, KG_MATRIX_FX_FIRE, 0, 99);
  uint16_t bottom = 0, top = 0;
  for (int i = 0; i < 200; i++) {
    matrix_fx_step(&fx, nullptr);
    for (uint8_t x = 0; x < KG_MATRIX_W; x++) {
      if (fx.fb[7][x]) bottom++;
      if (fx.fb[0][x]) top++;
    }
  }
  TEST_ASSERT_TRUE(bottom > top * 2);
  TEST_ASSERT_TRUE(bottom > 200 * 8);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_life_blinker);
  RUN_TEST(test_life_wraps);
  RUN_TEST(test_life_reseeds);
  RUN_TEST(test_clock);
  RUN_TEST(test_scanner_bounces);
  RUN_TEST(test_fire_rises);
  return UNITY_END();
}
