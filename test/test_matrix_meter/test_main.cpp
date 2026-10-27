#include <unity.h>
#include <string.h>
#include "matrix_meter.h"
#include "matrix_style.h"

void setUp() {}
void tearDown() {}

static uint8_t fb[KG_MATRIX_H][KG_MATRIX_W];

static uint8_t lit_in_row(uint8_t y) {
  uint8_t n = 0;
  for (uint8_t x = 0; x < KG_MATRIX_W; x++) n += fb[y][x] != KG_MX_OFF;
  return n;
}

static void test_style_names() {
  kg_mx_style_t s;
  kg_mx_style_default(&s);
  TEST_ASSERT_EQUAL(KG_MX_CLR_GREEN, s.opt[KG_MX_OPT_CLOCK_HOURS]);
  TEST_ASSERT_EQUAL(KG_MX_CLR_YELLOW, s.opt[KG_MX_OPT_CLOCK_MINUTES]);
  TEST_ASSERT_EQUAL(KG_MX_SEC_DOT, s.opt[KG_MX_OPT_CLOCK_SECONDS]);
  TEST_ASSERT_EQUAL(KG_MX_CLR_RED, s.opt[KG_MX_OPT_CLOCK_SECONDS_COLOR]);
  TEST_ASSERT_EQUAL(KG_MX_METER_BARS, s.opt[KG_MX_OPT_METER_STYLE]);
  TEST_ASSERT_FALSE(s.meter_peak);
  TEST_ASSERT_EQUAL_STRING("clock_seconds_color", kg_mx_opt_key(KG_MX_OPT_CLOCK_SECONDS_COLOR));
  TEST_ASSERT_EQUAL_STRING("Center", kg_mx_opt_name(KG_MX_OPT_METER_STYLE, KG_MX_METER_CENTER));
  TEST_ASSERT_EQUAL_STRING("?", kg_mx_opt_name(KG_MX_OPT_METER_STYLE, 99));
  TEST_ASSERT_EQUAL(5, kg_mx_opt_count(KG_MX_OPT_METER_STYLE));
  TEST_ASSERT_EQUAL(KG_MX_METER_VERTICAL, kg_mx_opt_parse(KG_MX_OPT_METER_STYLE, "vertical"));
  TEST_ASSERT_EQUAL(KG_MX_SEC_COLON, kg_mx_opt_parse(KG_MX_OPT_CLOCK_SECONDS, "COLON"));
  TEST_ASSERT_EQUAL(-1, kg_mx_opt_parse(KG_MX_OPT_CLOCK_SECONDS, "colons"));
  TEST_ASSERT_EQUAL(-1, kg_mx_opt_parse(KG_MX_OPT_CLOCK_HOURS, ""));
  TEST_ASSERT_EQUAL(KG_MX_RED, kg_mx_color(KG_MX_CLR_RED));
  TEST_ASSERT_EQUAL(KG_MX_GREEN, kg_mx_color(KG_MX_CLR_GREEN));
}

static void test_level_decay() {
  TEST_ASSERT_EQUAL(0, kg_meter_level(100));
  TEST_ASSERT_EQUAL(1, kg_meter_level(200));
  TEST_ASSERT_EQUAL(16, kg_meter_level(32000));
  TEST_ASSERT_EQUAL(16000 - 1000, kg_meter_decay(16000, KG_MX_DECAY_NORMAL));
  TEST_ASSERT_EQUAL(16000 - 500, kg_meter_decay(16000, KG_MX_DECAY_SLOW));
  TEST_ASSERT_EQUAL(16000 - 2000, kg_meter_decay(16000, KG_MX_DECAY_FAST));
  TEST_ASSERT_EQUAL(0, kg_meter_decay(1, KG_MX_DECAY_SLOW)); // reaches 0
  TEST_ASSERT_EQUAL(0, kg_meter_decay(0, KG_MX_DECAY_FAST));
}

static void test_bars_zones() {
  kg_mx_style_t s;
  kg_mx_style_default(&s);
  kg_meter_t m;
  kg_meter_init(&m);
  kg_meter_draw(&m, &s, 16, 4, fb);
  TEST_ASSERT_EQUAL(16, lit_in_row(1));
  TEST_ASSERT_EQUAL(16, lit_in_row(2));
  TEST_ASSERT_EQUAL(4, lit_in_row(5));
  TEST_ASSERT_EQUAL(0, lit_in_row(0));
  TEST_ASSERT_EQUAL(0, lit_in_row(3));
  TEST_ASSERT_EQUAL(KG_MX_GREEN, fb[1][12]);
  TEST_ASSERT_EQUAL(KG_MX_YELLOW, fb[1][13]);
  TEST_ASSERT_EQUAL(KG_MX_YELLOW, fb[1][14]);
  TEST_ASSERT_EQUAL(KG_MX_RED, fb[1][15]);
}

static void test_styles() {
  kg_mx_style_t s;
  kg_mx_style_default(&s);
  kg_meter_t m;
  s.opt[KG_MX_OPT_METER_STYLE] = KG_MX_METER_WIDE;
  kg_meter_init(&m);
  kg_meter_draw(&m, &s, 8, 2, fb);
  for (uint8_t y = 0; y < 4; y++) TEST_ASSERT_EQUAL(8, lit_in_row(y));
  for (uint8_t y = 4; y < 8; y++) TEST_ASSERT_EQUAL(2, lit_in_row(y));

  s.opt[KG_MX_OPT_METER_STYLE] = KG_MX_METER_CENTER;
  kg_meter_init(&m);
  kg_meter_draw(&m, &s, 16, 4, fb);
  // left: all 8 cells left of the middle, red at the edge; right: 2 cells from the middle
  TEST_ASSERT_EQUAL(KG_MX_GREEN, fb[3][7]);
  TEST_ASSERT_EQUAL(KG_MX_RED, fb[3][0]);
  TEST_ASSERT_EQUAL(KG_MX_GREEN, fb[3][8]);
  TEST_ASSERT_EQUAL(KG_MX_GREEN, fb[3][9]);
  TEST_ASSERT_EQUAL(KG_MX_OFF, fb[3][10]);

  s.opt[KG_MX_OPT_METER_STYLE] = KG_MX_METER_MONO;
  kg_meter_init(&m);
  kg_meter_draw(&m, &s, 3, 10, fb);
  for (uint8_t y = 0; y < 8; y++) TEST_ASSERT_EQUAL(10, lit_in_row(y));

  s.opt[KG_MX_OPT_METER_STYLE] = KG_MX_METER_VERTICAL;
  kg_meter_init(&m);
  kg_meter_draw(&m, &s, 16, 0, fb);
  TEST_ASSERT_EQUAL(KG_MX_GREEN, fb[7][0]);
  TEST_ASSERT_EQUAL(KG_MX_RED, fb[0][6]);
  TEST_ASSERT_EQUAL(KG_MX_OFF, fb[0][7]);
  TEST_ASSERT_EQUAL(KG_MX_OFF, fb[7][9]);
}

static void test_colors() {
  kg_mx_style_t s;
  kg_mx_style_default(&s);
  kg_meter_t m;
  kg_meter_init(&m);
  s.opt[KG_MX_OPT_METER_COLORS] = KG_MX_METER_COLORS_FIRE;
  kg_meter_draw(&m, &s, 6, 0, fb);
  TEST_ASSERT_EQUAL(KG_MX_YELLOW, fb[1][0]);
  TEST_ASSERT_EQUAL(KG_MX_RED, fb[1][5]);
  s.opt[KG_MX_OPT_METER_COLORS] = KG_MX_METER_COLORS_RED;
  kg_meter_draw(&m, &s, 16, 0, fb);
  TEST_ASSERT_EQUAL(KG_MX_RED, fb[1][0]);
  TEST_ASSERT_EQUAL(KG_MX_RED, fb[1][15]);
}

static void test_peak_hold() {
  kg_mx_style_t s;
  kg_mx_style_default(&s);
  s.meter_peak = true;
  kg_meter_t m;
  kg_meter_init(&m);
  kg_meter_draw(&m, &s, 10, 0, fb);
  // the level drops: the dot stays at 10 for the hold time
  for (uint8_t i = 0; i < KG_METER_HOLD_FRAMES; i++) {
    kg_meter_draw(&m, &s, 2, 0, fb);
    TEST_ASSERT_EQUAL(KG_MX_RED, fb[1][9]);
    TEST_ASSERT_EQUAL(KG_MX_OFF, fb[1][8]);
  }
  // then falls one level every KG_METER_FALL_FRAMES
  for (uint8_t i = 0; i < KG_METER_FALL_FRAMES; i++) kg_meter_draw(&m, &s, 2, 0, fb);
  TEST_ASSERT_EQUAL(9, m.peak[0]);
  TEST_ASSERT_EQUAL(KG_MX_RED, fb[1][8]);
  // down to the bar: no dot over it
  for (uint16_t i = 0; i < 200; i++) kg_meter_draw(&m, &s, 2, 0, fb);
  TEST_ASSERT_EQUAL(2, m.peak[0]);
  TEST_ASSERT_EQUAL(2, lit_in_row(1));
  // off: no dot
  s.meter_peak = false;
  kg_meter_init(&m);
  kg_meter_draw(&m, &s, 10, 0, fb);
  kg_meter_draw(&m, &s, 2, 0, fb);
  TEST_ASSERT_EQUAL(2, lit_in_row(1));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_style_names);
  RUN_TEST(test_level_decay);
  RUN_TEST(test_bars_zones);
  RUN_TEST(test_styles);
  RUN_TEST(test_colors);
  RUN_TEST(test_peak_hold);
  return UNITY_END();
}
