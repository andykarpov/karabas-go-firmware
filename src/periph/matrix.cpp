#include "main.h"

// Optional dot matrix addon (2x HT16K33 bi-color 8x8), see MATRIX.md

#define MATRIX_MODE_SCROLL 0
#define MATRIX_MODE_DRAW 1
#define MATRIX_MODE_AUDIO 2

static MultiMatrixDisplay matrix = MultiMatrixDisplay();
static bool has_matrix = false;
static uint8_t matrix_mode = MATRIX_MODE_SCROLL;
static uint16_t audio_l, audio_r;
static const String matrix_msg = "KARABAS GO ";
static const int matrix_msg_width = matrix_msg.length() * 6; // 6 pixels per character width
static int matrix_msg_scrollpos = 16;
static ElapsedTimer matrix_timer, matrix_scroll_timer, matrix_poll_timer;

void matrix_init()
{
  if (has_matrix) {
    // todo: get from global config ?
    matrix.setTextWrap(false);
    matrix.setTextSize(1);
    matrix.setTextColor(LED_YELLOW);
    matrix.setBrightness(4); // safe enough
  }
}

void matrix_ctl_data(uint8_t addr, uint8_t data) {
  if (has_matrix) {
    if (bitRead(data, 0)) matrix.clear(); 
    if (bitRead(data, 1)) matrix.writeDisplay();
    if (bitRead(data, 2)) matrix.setBrightness(data >> 4);
    if (bitRead(data, 3) && matrix_mode == MATRIX_MODE_DRAW) {
      matrix_mode = MATRIX_MODE_AUDIO;
      matrix.clear();
      matrix.writeDisplay();
    }
    if (!bitRead(data, 3) && matrix_mode == MATRIX_MODE_AUDIO) {
      matrix_mode = MATRIX_MODE_DRAW;
      matrix.clear();
      matrix.writeDisplay();
    }
  }
}

void matrix_pixel_data(uint8_t x, uint8_t y, uint8_t color) {
  if (has_matrix) {
    matrix.drawPixel(x, y, color);
  }
}

void matrix_bitplan_data(uint8_t addr, uint8_t data) {
  if (has_matrix) {
    // todo:
    // addr 0-7:   matrix1, red
    // addr 8-15:  matrix1, green
    // addr 16-23: matrix2, red
    // addr 24-31: matrix2, green
    //matrix.drawBitmap()
  }
}

uint8_t led_bar(uint16_t val) {
  uint16_t values[16] = {20000, 16000, 10000, 8000, 6500, 5000, 3500, 3000, 2000, 1500, 1000, 750, 600, 500, 350, 200};
  for (uint8_t i=0; i<=15; i++) {
    if (val >= values[i]) 
      return constrain(16-i, 1, 16);
  }
  return 0;
}

void matrix_draw_bar(uint8_t y, uint8_t val) {
  if (val > 0) {
    for (uint8_t x=0; x<=15; x++) {
      if (val >= x) {
          matrix.drawPixel(x, y, (x > 14) ? LED_RED : (x > 12) ? LED_YELLOW : LED_GREEN);
      }
    }
  }
}

void matrix_draw_audio() {
  matrix.clear();
  uint8_t l = led_bar(audio_l);
  uint8_t r = led_bar(audio_r);
  matrix_draw_bar(1, l);
  matrix_draw_bar(2, l);
  matrix_draw_bar(5, r);
  matrix_draw_bar(6, r);
  matrix.writeDisplay();
  
  // decay
  uint16_t decay_l = audio_l >> 4;
  uint16_t decay_r = audio_r >> 4;
  audio_l = (audio_l > decay_l) ? audio_l - decay_l : 0;
  audio_r = (audio_r > decay_r) ? audio_r - decay_r : 0;
}

void matrix_scroll_text()
{
  matrix.clear();
  matrix.setCursor(matrix_msg_scrollpos, 0);
  matrix.print(matrix_msg);
  matrix.writeDisplay();
  if (matrix_msg_scrollpos >= -matrix_msg_width) {
    matrix_msg_scrollpos--;
  } else {
    matrix_msg_scrollpos = 16;
  }
}

void matrix_begin() {
  has_matrix = matrix.begin();
  if (has_matrix) {
    d_println("Found a matrix display");
  } else {
    d_println("Matrix display was not found");
  }

  matrix_init();
}

void matrix_register_handlers() {
  link_on(CMD_MATRIX_CTL, matrix_ctl_data);
  link_on(CMD_MATRIX_PIXEL, [](uint8_t addr, uint8_t data) { matrix_mode = MATRIX_MODE_DRAW; matrix_pixel_data(addr & 0x0F, addr >> 4, data & 0x03); });
  link_on(CMD_MATRIX_BITPLAN, [](uint8_t addr, uint8_t data) { matrix_mode = MATRIX_MODE_DRAW; matrix_bitplan_data(addr & 0x1F, data); });
  link_on(CMD_AUDIO_PEAKS_L, [](uint8_t addr, uint8_t data) { uint16_t new_audio = ((uint16_t)(addr) << 8) | (uint16_t)data; if (new_audio > audio_l) audio_l = new_audio; });
  link_on(CMD_AUDIO_PEAKS_R, [](uint8_t addr, uint8_t data) { uint16_t new_audio = ((uint16_t)(addr) << 8) | (uint16_t)data; if (new_audio > audio_r) audio_r = new_audio; });
}

void matrix_progress(uint32_t val, uint32_t max) {
  if (has_matrix) {
    matrix.drawLine(0, 0, map(val, 0, max, 0, 15), 0, LED_GREEN);
    matrix.drawLine(0, 1, map(val, 0, max, 0, 15), 1, LED_GREEN);
    matrix.writeDisplay();
  }
}

void matrix_on_configure_start() {
  matrix_mode = MATRIX_MODE_AUDIO;
  if (has_matrix) {
    matrix.clear();
    matrix.writeDisplay();
  }
}

void matrix_on_configure_done(uint8_t core_type) {
  if (has_matrix) {
    matrix_init();
    matrix.clear();
    matrix.writeDisplay();
    matrix_mode = (core_type == CORE_TYPE_BOOT) ? MATRIX_MODE_SCROLL : MATRIX_MODE_AUDIO;
    audio_l = 32000;
    audio_r = 32000;
  }
}

void matrix_tick() {
  if (has_matrix && matrix_timer.elapsed() >= 20) {
    matrix_timer.reset();

    switch (matrix_mode) {
      case MATRIX_MODE_AUDIO:
        matrix_draw_audio();
        break;
      case MATRIX_MODE_SCROLL:
        if (matrix_scroll_timer.elapsed() >= 100) {
          matrix_scroll_timer.reset();
          matrix_scroll_text();
        }
        break;
    }

    static bool prev_matrix_btn1 = false;
    static bool prev_matrix_btn2 = false;
    static bool prev_matrix_btn3 = false;
    static bool prev_matrix_btn4 = false;

    // poll HT16k33 for buttons
    if (matrix_poll_timer.elapsed() >= 100) {
      matrix_poll_timer.reset();
      uint8_t matrix_btns = matrix.readKeys();
      bool matrix_btn1 = bitRead(matrix_btns, 0);
      bool matrix_btn2 = bitRead(matrix_btns, 1);
      bool matrix_btn3 = bitRead(matrix_btns, 2);
      bool matrix_btn4 = bitRead(matrix_btns, 3);
      if (prev_matrix_btn1 != matrix_btn1) {
        d_print("Matrix Button 1: "); d_println((matrix_btn1) ? "on" : "off");
        prev_matrix_btn1 = matrix_btn1;
        spi_send(CMD_MATRIX_BTN, 0, matrix_btn1);
      }
      if (prev_matrix_btn2 != matrix_btn2) {
        d_print("Matrix Button 2: "); d_println((matrix_btn2) ? "on" : "off");
        prev_matrix_btn2 = matrix_btn2;
        spi_send(CMD_MATRIX_BTN, 1, matrix_btn2);
      }
      if (prev_matrix_btn3 != matrix_btn3) {
        d_print("Matrix Button 3: "); d_println((matrix_btn3) ? "on" : "off");
        prev_matrix_btn3 = matrix_btn3;
        spi_send(CMD_MATRIX_BTN, 2, matrix_btn3);
      }
      if (prev_matrix_btn4 != matrix_btn4) {
        d_print("Matrix Button 4: "); d_println((matrix_btn4) ? "on" : "off");
        prev_matrix_btn4 = matrix_btn4;
        spi_send(CMD_MATRIX_BTN, 3, matrix_btn4);
      }
    }
  }
}
