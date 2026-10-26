#include <string.h>
#include <unity.h>
#include "kg_matrix.h"

void setUp() {}
void tearDown() {}

static void test_parse_zero_block_is_core() {
  uint8_t block[KG_MATRIX_BLOCK_SIZE] = {0};
  kg_matrix_cfg_t cfg;
  kg_matrix_parse(block, &cfg);
  TEST_ASSERT_EQUAL_UINT8(KG_MATRIX_MODE_CORE, cfg.mode);
  TEST_ASSERT_EQUAL_UINT8(KG_MATRIX_DEFAULT_BRIGHTNESS, cfg.brightness);
  TEST_ASSERT_EQUAL_UINT8(1, cfg.frame_count);
  TEST_ASSERT_EQUAL_STRING("", cfg.text);
}

static void test_parse_ff_block_is_core() {
  uint8_t block[KG_MATRIX_BLOCK_SIZE];
  memset(block, 0xFF, sizeof(block));
  kg_matrix_cfg_t cfg;
  kg_matrix_parse(block, &cfg);
  TEST_ASSERT_EQUAL_UINT8(KG_MATRIX_MODE_CORE, cfg.mode);
  TEST_ASSERT_EQUAL_UINT8(KG_MATRIX_COLOR_DEFAULT, cfg.color);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.speed);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.repeat);
  TEST_ASSERT_EQUAL_UINT8(KG_MATRIX_MODE_CORE, cfg.then);
  TEST_ASSERT_EQUAL_UINT8(KG_MATRIX_DEFAULT_BRIGHTNESS, cfg.brightness);
  TEST_ASSERT_EQUAL_STRING("", cfg.text);
}

static void test_parse_scroll() {
  uint8_t block[KG_MATRIX_BLOCK_SIZE] = {0};
  block[0] = KG_MATRIX_MODE_SCROLL;
  block[1] = KG_MATRIX_COLOR_RAINBOW;
  block[2] = 3;
  block[3] = 2;
  block[4] = KG_MATRIX_MODE_EFFECT;
  block[5] = KG_MATRIX_FX_FIRE;
  block[6] = 16;
  memcpy(block + KG_MATRIX_POS_TEXT, "NES {build}", 11);
  kg_matrix_cfg_t cfg;
  kg_matrix_parse(block, &cfg);
  TEST_ASSERT_EQUAL_UINT8(KG_MATRIX_MODE_SCROLL, cfg.mode);
  TEST_ASSERT_EQUAL_UINT8(KG_MATRIX_COLOR_RAINBOW, cfg.color);
  TEST_ASSERT_EQUAL_UINT8(3, cfg.speed);
  TEST_ASSERT_EQUAL_UINT8(2, cfg.repeat);
  TEST_ASSERT_EQUAL_UINT8(KG_MATRIX_MODE_EFFECT, cfg.then);
  TEST_ASSERT_EQUAL_UINT8(KG_MATRIX_FX_FIRE, cfg.effect);
  TEST_ASSERT_EQUAL_UINT8(15, cfg.brightness);
  TEST_ASSERT_EQUAL_STRING("NES {build}", cfg.text);
}

static void test_parse_full_text_and_bad_chars() {
  uint8_t block[KG_MATRIX_BLOCK_SIZE] = {0};
  block[0] = KG_MATRIX_MODE_SCROLL;
  memset(block + KG_MATRIX_POS_TEXT, 'A', KG_MATRIX_TEXT_LEN);
  block[KG_MATRIX_POS_TEXT + 1] = 0x81;
  kg_matrix_cfg_t cfg;
  kg_matrix_parse(block, &cfg);
  TEST_ASSERT_EQUAL_UINT32(KG_MATRIX_TEXT_LEN, strlen(cfg.text));
  TEST_ASSERT_EQUAL_CHAR('?', cfg.text[1]);
}

static void test_parse_scroll_without_text_is_core() {
  uint8_t block[KG_MATRIX_BLOCK_SIZE] = {0};
  block[0] = KG_MATRIX_MODE_SCROLL;
  kg_matrix_cfg_t cfg;
  kg_matrix_parse(block, &cfg);
  TEST_ASSERT_EQUAL_UINT8(KG_MATRIX_MODE_CORE, cfg.mode);
}

static void test_parse_then_never_scroll_or_image() {
  uint8_t block[KG_MATRIX_BLOCK_SIZE] = {0};
  kg_matrix_cfg_t cfg;
  block[4] = KG_MATRIX_MODE_SCROLL;
  kg_matrix_parse(block, &cfg);
  TEST_ASSERT_EQUAL_UINT8(KG_MATRIX_MODE_CORE, cfg.then);
  block[4] = KG_MATRIX_MODE_IMAGE;
  kg_matrix_parse(block, &cfg);
  TEST_ASSERT_EQUAL_UINT8(KG_MATRIX_MODE_CORE, cfg.then);
  block[4] = KG_MATRIX_MODE_BLANK;
  kg_matrix_parse(block, &cfg);
  TEST_ASSERT_EQUAL_UINT8(KG_MATRIX_MODE_BLANK, cfg.then);
}

static void test_parse_frames() {
  uint8_t block[KG_MATRIX_BLOCK_SIZE] = {0};
  block[0] = KG_MATRIX_MODE_IMAGE;
  block[7] = 4;
  block[KG_MATRIX_POS_FRAMES + 3 * KG_MATRIX_FRAME_SIZE] = 0x80;
  kg_matrix_cfg_t cfg;
  kg_matrix_parse(block, &cfg);
  TEST_ASSERT_EQUAL_UINT8(4, cfg.frame_count);
  TEST_ASSERT_EQUAL_UINT8(KG_MX_RED, kg_matrix_frame_pixel(cfg.frames[3], 0, 0));
  block[7] = 5;
  kg_matrix_parse(block, &cfg);
  TEST_ASSERT_EQUAL_UINT8(1, cfg.frame_count);
}

static void test_frame_pixel() {
  uint8_t frame[KG_MATRIX_FRAME_SIZE] = {0};
  frame[0] = 0x80;       // red x=0 y=0
  frame[16 + 0] = 0x80;  // green x=0 y=0 -> yellow
  frame[16 + 15] = 0x01; // green x=15 y=7
  frame[2 * 3 + 1] = 0x10; // red x=11 y=3
  TEST_ASSERT_EQUAL_UINT8(KG_MX_YELLOW, kg_matrix_frame_pixel(frame, 0, 0));
  TEST_ASSERT_EQUAL_UINT8(KG_MX_GREEN, kg_matrix_frame_pixel(frame, 15, 7));
  TEST_ASSERT_EQUAL_UINT8(KG_MX_RED, kg_matrix_frame_pixel(frame, 11, 3));
  TEST_ASSERT_EQUAL_UINT8(KG_MX_OFF, kg_matrix_frame_pixel(frame, 1, 0));
  TEST_ASSERT_EQUAL_UINT8(KG_MX_OFF, kg_matrix_frame_pixel(frame, 16, 0));
}

static void test_text_color() {
  TEST_ASSERT_EQUAL_UINT8(KG_MX_YELLOW, kg_matrix_text_color(KG_MATRIX_COLOR_DEFAULT, 0, 0));
  TEST_ASSERT_EQUAL_UINT8(KG_MX_GREEN, kg_matrix_text_color(KG_MATRIX_COLOR_GREEN, 5, 2));
  TEST_ASSERT_EQUAL_UINT8(KG_MX_RED, kg_matrix_text_color(KG_MATRIX_COLOR_RAINBOW, 0, 0));
  TEST_ASSERT_EQUAL_UINT8(KG_MX_YELLOW, kg_matrix_text_color(KG_MATRIX_COLOR_RAINBOW, 0, 1));
  TEST_ASSERT_EQUAL_UINT8(KG_MX_GREEN, kg_matrix_text_color(KG_MATRIX_COLOR_RAINBOW, 0, 5));
  TEST_ASSERT_EQUAL_UINT8(KG_MX_GREEN, kg_matrix_text_color(KG_MATRIX_COLOR_GRADIENT, 0, 0));
  TEST_ASSERT_EQUAL_UINT8(KG_MX_YELLOW, kg_matrix_text_color(KG_MATRIX_COLOR_GRADIENT, 8, 0));
  TEST_ASSERT_EQUAL_UINT8(KG_MX_RED, kg_matrix_text_color(KG_MATRIX_COLOR_GRADIENT, 15, 0));
}

static const kg_matrix_vars_t vars = {"ZX Spectrum", "20261026", 9, 5, 1, 2, 2026};

static void test_expand() {
  char out[128];
  kg_matrix_expand("{name} {build} {time} {date}", &vars, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("ZX Spectrum 20261026 09:05 01.02.2026", out);
}

static void test_expand_unknown_and_braces() {
  char out[64];
  kg_matrix_expand("a {x} {name", &vars, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("a {x} {name", out);
}

static void test_expand_truncates() {
  char out[8];
  size_t len = kg_matrix_expand("x {name}", &vars, out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT32(7, len);
  TEST_ASSERT_EQUAL_STRING("x ZX Sp", out);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_zero_block_is_core);
  RUN_TEST(test_parse_ff_block_is_core);
  RUN_TEST(test_parse_scroll);
  RUN_TEST(test_parse_full_text_and_bad_chars);
  RUN_TEST(test_parse_scroll_without_text_is_core);
  RUN_TEST(test_parse_then_never_scroll_or_image);
  RUN_TEST(test_parse_frames);
  RUN_TEST(test_frame_pixel);
  RUN_TEST(test_text_color);
  RUN_TEST(test_expand);
  RUN_TEST(test_expand_unknown_and_braces);
  RUN_TEST(test_expand_truncates);
  return UNITY_END();
}
