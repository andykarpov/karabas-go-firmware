#include "main.h"
#include "i2c_bus.h"

// Optional dot matrix addon (2x HT16K33 bi-color 8x8), see MATRIX.md
//
// The display buffer and the mode are shared by the fpga command handlers (APP) and
// matrix_frame() (MATRIX task): every access to `matrix` holds the I2C lock.

#define MATRIX_MODE_SCROLL 0
#define MATRIX_MODE_DRAW 1
#define MATRIX_MODE_AUDIO 2
#define MATRIX_MODE_LOADING 3 // core load in progress: the loader draws its progress bar
#define MATRIX_MODE_ICON 4    // a static icon (msc, bootsel): nothing is redrawn

#define MATRIX_FRAME_MS 20      // frame period
#define MATRIX_SCROLL_FRAMES 5  // scroll text one pixel every 5 frames (100 ms)
#define MATRIX_POLL_FRAMES 5    // poll the buttons every 5 frames (100 ms)

static MultiMatrixDisplay matrix = MultiMatrixDisplay();
static bool has_matrix = false;
static volatile uint8_t matrix_mode = MATRIX_MODE_SCROLL;
static volatile uint16_t audio_l, audio_r; // peak meters: raised by APP, decayed by matrix_frame
static const String matrix_msg = "KARABAS GO ";
static const int matrix_msg_width = matrix_msg.length() * 6; // 6 pixels per character width
static int matrix_msg_scrollpos = 16;

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
    I2cGuard i2c;
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
    I2cGuard i2c;
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
  I2cGuard i2c;
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
    I2cGuard i2c;
    matrix.drawLine(0, 0, map(val, 0, max, 0, 15), 0, LED_GREEN);
    matrix.drawLine(0, 1, map(val, 0, max, 0, 15), 1, LED_GREEN);
    matrix.writeDisplay();
  }
}

void matrix_on_configure_start() {
  matrix_mode = MATRIX_MODE_LOADING;
  if (has_matrix) {
    I2cGuard i2c;
    matrix.clear();
    matrix.writeDisplay();
  }
}

void matrix_on_configure_done(uint8_t core_type) {
  if (has_matrix) {
    I2cGuard i2c;
    matrix_init();
    matrix.clear();
    matrix.writeDisplay();
    matrix_mode = (core_type == CORE_TYPE_BOOT) ? MATRIX_MODE_SCROLL : MATRIX_MODE_AUDIO;
    audio_l = 32000;
    audio_r = 32000;
  }
}

// 16x8 icons: G green, R red, Y yellow, anything else off
static const char *const matrix_icons[][8] = {
  { // MATRIX_ICON_MSC: the SD card goes to the PC
    "GGGGG...........",
    "G.G.GG......Y...",
    "G.G.G.G.....YY..",
    "G.....G.YYYYYYY.",
    "G.....G.YYYYYYY.",
    "G.....G.....YY..",
    "G.....G.....Y...",
    "GGGGGGG.........",
  },
  { // MATRIX_ICON_BOOTSEL: the firmware goes into the chip
    ".........R.R.R..",
    "....Y...RRRRRRR.",
    "....YY..R.....R.",
    "YYYYYYY.R.....R.",
    "YYYYYYY.R.....R.",
    "....YY..R.....R.",
    "....Y...RRRRRRR.",
    ".........R.R.R..",
  },
};

void matrix_icon(uint8_t icon) {
  if (!has_matrix || icon >= sizeof(matrix_icons) / sizeof(matrix_icons[0])) return;
  I2cGuard i2c;
  matrix_mode = MATRIX_MODE_ICON;
  matrix.clear();
  for (uint8_t y=0; y<8; y++) {
    for (uint8_t x=0; x<16; x++) {
      char c = matrix_icons[icon][y][x];
      if (c == 'G') matrix.drawPixel(x, y, LED_GREEN);
      else if (c == 'R') matrix.drawPixel(x, y, LED_RED);
      else if (c == 'Y') matrix.drawPixel(x, y, LED_YELLOW);
    }
  }
  matrix.writeDisplay();
}

bool matrix_present() {
  return has_matrix;
}

// one display frame, called every MATRIX_FRAME_MS (scroll and button poll are counted in
// frames, so they stay evenly spaced however the frames are scheduled)
void matrix_frame() {
  if (!has_matrix) return;
  static uint32_t frame = 0;
  frame++;

  static uint8_t prev_btns = 0;
  uint8_t btns = prev_btns;
  {
    I2cGuard i2c;
    switch (matrix_mode) {
      case MATRIX_MODE_AUDIO:
        matrix_draw_audio();
        break;
      case MATRIX_MODE_SCROLL:
        if (frame % MATRIX_SCROLL_FRAMES == 0) {
          matrix_scroll_text();
        }
        break;
    }

    // poll HT16k33 for buttons
    if (frame % MATRIX_POLL_FRAMES == 0) {
      btns = matrix.readKeys() & 0x0F;
    }
  }

  // report changed buttons outside the I2C lock (lock order: never I2C -> SPI)
  for (uint8_t i=0; i<4; i++) {
    bool btn = bitRead(btns, i);
    if (btn != bitRead(prev_btns, i)) {
      d_print("Matrix Button "); d_print(i+1); d_print(": "); d_println(btn ? "on" : "off");
      spi_send(CMD_MATRIX_BTN, i, btn);
    }
  }
  prev_btns = btns;
}
