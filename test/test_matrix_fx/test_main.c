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
  for (int i = 0; i < 2000; i++) matrix_fx_step(&fx, NULL);
  // still something alive (dead fields are seeded again)
  uint16_t any = 0;
  for (uint8_t y = 0; y < KG_MATRIX_H; y++) any |= fx.cells[y];
  TEST_ASSERT_NOT_EQUAL(0, any);
}

static void test_clock() {
  matrix_fx_t fx;
  matrix_fx_init(&fx, KG_MATRIX_FX_CLOCK, 0, 1);
  matrix_fx_input_t t = {};
  t.hour = 12; t.minute = 34; t.second = 59;
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

static void test_clock_style() {
  matrix_fx_t fx;
  matrix_fx_init(&fx, KG_MATRIX_FX_CLOCK, 0, 1);
  kg_mx_style_t st;
  kg_mx_style_default(&st);
  st.opt[KG_MX_OPT_CLOCK_HOURS] = KG_MX_CLR_RED;
  st.opt[KG_MX_OPT_CLOCK_MINUTES] = KG_MX_CLR_GREEN;
  st.opt[KG_MX_OPT_CLOCK_SECONDS] = KG_MX_SEC_COLON;
  st.opt[KG_MX_OPT_CLOCK_SECONDS_COLOR] = KG_MX_CLR_YELLOW;
  matrix_fx_input_t t = {};
  t.hour = 12; t.minute = 34; t.second = 10;
  t.style = &st;
  matrix_fx_step(&fx, &t);
  TEST_ASSERT_EQUAL_UINT8(KG_MX_RED, fx.fb[1][1]);
  TEST_ASSERT_EQUAL_UINT8(KG_MX_GREEN, fx.fb[1][13]);
  // the colon on even seconds, no dot on the bottom row
  TEST_ASSERT_EQUAL_UINT8(KG_MX_YELLOW, fx.fb[2][7]);
  TEST_ASSERT_EQUAL_UINT8(KG_MX_YELLOW, fx.fb[4][8]);
  for (uint8_t x = 0; x < 16; x++) TEST_ASSERT_EQUAL_UINT8(KG_MX_OFF, fx.fb[7][x]);
  t.second = 11;
  matrix_fx_step(&fx, &t);
  TEST_ASSERT_EQUAL_UINT8(KG_MX_OFF, fx.fb[2][7]);
  // the bar: 30 s = 9 cells
  st.opt[KG_MX_OPT_CLOCK_SECONDS] = KG_MX_SEC_BAR;
  t.second = 30;
  matrix_fx_step(&fx, &t);
  TEST_ASSERT_EQUAL_UINT8(KG_MX_YELLOW, fx.fb[7][8]);
  TEST_ASSERT_EQUAL_UINT8(KG_MX_OFF, fx.fb[7][9]);
  st.opt[KG_MX_OPT_CLOCK_SECONDS] = KG_MX_SEC_OFF;
  matrix_fx_step(&fx, &t);
  for (uint8_t x = 0; x < 16; x++) TEST_ASSERT_EQUAL_UINT8(KG_MX_OFF, fx.fb[7][x]);
}

static void test_scanner_bounces() {
  matrix_fx_t fx;
  matrix_fx_init(&fx, KG_MATRIX_FX_SCANNER, 0, 1);
  for (int i = 0; i < 100; i++) {
    matrix_fx_step(&fx, NULL);
    TEST_ASSERT_TRUE(fx.pos >= 0 && fx.pos < KG_MATRIX_W);
    TEST_ASSERT_TRUE(count(&fx, KG_MX_RED) >= 4); // the head
    TEST_ASSERT_EQUAL_UINT8(0, count(&fx, KG_MX_GREEN));
  }
  matrix_fx_init(&fx, KG_MATRIX_FX_SCANNER, KG_MX_GREEN, 1);
  matrix_fx_step(&fx, NULL);
  TEST_ASSERT_EQUAL_UINT8(4, count(&fx, KG_MX_GREEN));
}

static void test_fire_rises() {
  matrix_fx_t fx;
  matrix_fx_init(&fx, KG_MATRIX_FX_FIRE, 0, 99);
  uint16_t bottom = 0, top = 0;
  for (int i = 0; i < 200; i++) {
    matrix_fx_step(&fx, NULL);
    for (uint8_t x = 0; x < KG_MATRIX_W; x++) {
      if (fx.fb[7][x]) bottom++;
      if (fx.fb[0][x]) top++;
    }
  }
  TEST_ASSERT_TRUE(bottom > top * 2);
  TEST_ASSERT_TRUE(bottom > 200 * 8);
}

static void test_sin8() {
  TEST_ASSERT_EQUAL_INT8(0, matrix_fx_sin8(0));
  TEST_ASSERT_EQUAL_INT8(127, matrix_fx_sin8(64));
  TEST_ASSERT_EQUAL_INT8(0, matrix_fx_sin8(128));
  TEST_ASSERT_EQUAL_INT8(-127, matrix_fx_sin8(192));
  TEST_ASSERT_EQUAL_INT8(-matrix_fx_sin8(20), matrix_fx_sin8(148));
}

static void test_rain() {
  matrix_fx_t fx;
  matrix_fx_init(&fx, KG_MATRIX_FX_RAIN, 0, 7);
  uint16_t lit = 0;
  for (int i = 0; i < 300; i++) {
    matrix_fx_step(&fx, NULL);
    TEST_ASSERT_EQUAL_UINT8(0, count(&fx, KG_MX_RED));
    // a column has at most one head
    for (uint8_t x = 0; x < KG_MATRIX_W; x++) {
      uint8_t heads = 0;
      for (uint8_t y = 0; y < KG_MATRIX_H; y++) if (fx.fb[y][x] == KG_MX_YELLOW) heads++;
      TEST_ASSERT_TRUE(heads <= 1);
    }
    lit += count(&fx, KG_MX_GREEN);
  }
  TEST_ASSERT_TRUE(lit > 300);
}

static void test_plasma_moves() {
  matrix_fx_t fx;
  matrix_fx_init(&fx, KG_MATRIX_FX_PLASMA, 0, 1);
  matrix_fx_step(&fx, NULL);
  uint8_t first[KG_MATRIX_H][KG_MATRIX_W];
  memcpy(first, fx.fb, sizeof(first));
  for (int i = 0; i < 5; i++) matrix_fx_step(&fx, NULL);
  TEST_ASSERT_TRUE(memcmp(first, fx.fb, sizeof(first)) != 0);
  // all four bands show up
  TEST_ASSERT_TRUE(count(&fx, KG_MX_OFF) + count(&fx, KG_MX_GREEN) + count(&fx, KG_MX_YELLOW) + count(&fx, KG_MX_RED) == 128);
}

static void test_stars() {
  matrix_fx_t fx;
  matrix_fx_init(&fx, KG_MATRIX_FX_STARS, 0, 3);
  for (int i = 0; i < 500; i++) {
    matrix_fx_step(&fx, NULL);
    uint8_t lit = 128 - count(&fx, KG_MX_OFF);
    TEST_ASSERT_TRUE(lit > 0 && lit <= MATRIX_FX_STARS);
  }
}

static void test_pong() {
  matrix_fx_t fx;
  matrix_fx_init(&fx, KG_MATRIX_FX_PONG, 0, 5);
  uint16_t bounces = 0;
  int8_t dx = fx.ball_dx;
  for (int i = 0; i < 2000; i++) {
    matrix_fx_step(&fx, NULL);
    // the paddles, 3 pixels each (the ball may stand on one of them after a miss)
    for (uint8_t k = 0; k < 3; k++) {
      uint8_t l = fx.fb[fx.paddle[0] + k][0], r = fx.fb[fx.paddle[1] + k][KG_MATRIX_W - 1];
      TEST_ASSERT_TRUE(l == KG_MX_GREEN || l == KG_MX_YELLOW);
      TEST_ASSERT_TRUE(r == KG_MX_RED || r == KG_MX_YELLOW);
    }
    TEST_ASSERT_TRUE(count(&fx, KG_MX_GREEN) >= 2 && count(&fx, KG_MX_GREEN) <= 3);
    TEST_ASSERT_TRUE(count(&fx, KG_MX_YELLOW) <= 1);
    TEST_ASSERT_TRUE(fx.ball_y >= 0 && fx.ball_y <= (KG_MATRIX_H - 1) * 16);
    TEST_ASSERT_TRUE(fx.paddle[0] >= 0 && fx.paddle[0] <= KG_MATRIX_H - 3);
    if (fx.ball_dx != dx && fx.serve_wait == 0) { bounces++; dx = fx.ball_dx; }
  }
  TEST_ASSERT_TRUE(bounces > 20);
}

static void test_drive() {
  matrix_fx_t fx;
  matrix_fx_input_t in = {};
  matrix_fx_init(&fx, KG_MATRIX_FX_DRIVE, 0, 1);
  // no image: a dotted line
  matrix_fx_step(&fx, &in);
  TEST_ASSERT_EQUAL_UINT8(KG_MX_RED, fx.fb[3][1]);
  TEST_ASSERT_EQUAL_UINT8(0, count(&fx, KG_MX_YELLOW));
  // one image, a read at the end: 4 rows of the head at the last column, the dot green
  in.drive[2].sectors = 1760;
  in.drive[2].lba = 1759;
  in.drive[2].reads = 1;
  matrix_fx_step(&fx, &in);
  TEST_ASSERT_EQUAL_UINT8(4, count(&fx, KG_MX_YELLOW));
  TEST_ASSERT_EQUAL_UINT8(KG_MX_YELLOW, fx.fb[2][15]);
  TEST_ASSERT_EQUAL_UINT8(8, count(&fx, KG_MX_GREEN));
  // a write: red; then the dot goes out
  in.drive[2].writes = 1;
  in.drive[2].lba = 0;
  matrix_fx_step(&fx, &in);
  TEST_ASSERT_EQUAL_UINT8(8, count(&fx, KG_MX_RED));
  TEST_ASSERT_EQUAL_UINT8(KG_MX_YELLOW, fx.fb[2][3]);
  for (int i = 0; i < 8; i++) matrix_fx_step(&fx, &in);
  TEST_ASSERT_EQUAL_UINT8(0, count(&fx, KG_MX_RED));
  // two images: 3 rows each
  in.drive[5].sectors = 100;
  matrix_fx_step(&fx, &in);
  TEST_ASSERT_EQUAL_UINT8(6, count(&fx, KG_MX_YELLOW));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_life_blinker);
  RUN_TEST(test_life_wraps);
  RUN_TEST(test_life_reseeds);
  RUN_TEST(test_clock);
  RUN_TEST(test_clock_style);
  RUN_TEST(test_scanner_bounces);
  RUN_TEST(test_fire_rises);
  RUN_TEST(test_sin8);
  RUN_TEST(test_rain);
  RUN_TEST(test_plasma_moves);
  RUN_TEST(test_stars);
  RUN_TEST(test_pong);
  RUN_TEST(test_drive);
  return UNITY_END();
}
