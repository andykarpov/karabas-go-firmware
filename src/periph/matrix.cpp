#include "main.h"
#include "i2c_bus.h"
#include "kg_matrix.h"
#include "matrix_fx.h"
#include "matrix_meter.h"

// Optional dot matrix addon (2x HT16K33 bi-color 8x8), see MATRIX.md
//
// The display buffer and the mode are shared by the fpga command handlers (APP) and
// matrix_frame() (MATRIX task): every access to `matrix` holds the I2C lock.

#define MATRIX_MODE_SCROLL 0
#define MATRIX_MODE_DRAW 1    // the core draws (CMD_MATRIX_PIXEL / BITPLAN)
#define MATRIX_MODE_AUDIO 2   // the audio meters of the core
#define MATRIX_MODE_LOADING 3 // core load in progress: the loader draws its progress bar
#define MATRIX_MODE_ICON 4    // a static icon (msc, bootsel): nothing is redrawn
#define MATRIX_MODE_EFFECT 5
#define MATRIX_MODE_IMAGE 6
#define MATRIX_MODE_BLANK 7

#define MATRIX_FRAME_MS 20      // frame period
#define MATRIX_SCROLL_FRAMES 5  // default: scroll text one pixel every 5 frames (100 ms)
#define MATRIX_IMAGE_FRAMES 10  // default: next image frame every 10 frames (200 ms)
#define MATRIX_POLL_FRAMES 5    // poll the buttons every 5 frames (100 ms)

static MultiMatrixDisplay matrix = MultiMatrixDisplay();
static bool has_matrix = false;
static volatile uint8_t matrix_mode = MATRIX_MODE_SCROLL;
static volatile uint16_t audio_l, audio_r; // peak meters: raised by APP, decayed by matrix_frame

// the settings of the core (core.json "matrix", FILE_POS_MATRIX), set by read_core()
static kg_matrix_cfg_t cfg;
static char cfg_name[32+1], cfg_build[8+1];
static uint8_t cfg_core_type;

// karabas.ini [matrix] as shown now: hw_setup, or the values being edited in Settings
static bool mx_enabled = true;
static uint8_t mx_brightness = 0; // 0: the core's
static kg_mx_style_t mx_style;    // the clock and the meters
static uint8_t preview = MATRIX_PREVIEW_NONE; // Settings shows a plugin while its options are edited
static kg_meter_t meter;
static uint32_t demo_rnd = 0x1234567;
static uint8_t demo_wait[2]; // frames to the next beat of the demo signal

// the boot core without settings: the scroll of old
static const char *const matrix_boot_text = "KARABAS GO ";

// the running mode: steps every `step_frames` frames, `repeat` passes / loops (0 = forever)
static uint16_t step_frames, step_count;
static uint8_t passes;

static GFXcanvas1 text_canvas(16, 8);
static const char *scroll_src;
static uint8_t scroll_color, scroll_repeat;
static char scroll_text[128];
static int scroll_width;
static int scroll_pos;

static matrix_fx_t fx;
static uint8_t image_frame;

// the image slots for the drive effect: written by APP (img_mount), read by matrix_frame; a torn
// value only draws one step wrong
static matrix_fx_drive_t drives[MATRIX_FX_DRIVES];

void matrix_init()
{
  if (has_matrix) {
    // karabas.ini over the core; KG_MATRIX_DEFAULT_BRIGHTNESS (4) is safe enough
    matrix.setBrightness(mx_brightness ? mx_brightness - 1 : cfg.brightness);
    text_canvas.setTextWrap(false);
  }
}

// the core draws or shows its meters only in the "core" mode of its settings
static bool core_owns_matrix() {
  return matrix_mode == MATRIX_MODE_AUDIO || matrix_mode == MATRIX_MODE_DRAW;
}

void matrix_ctl_data(uint8_t addr, uint8_t data) {
  if (has_matrix) {
    I2cGuard i2c;
    if (!core_owns_matrix()) return;
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
    if (!core_owns_matrix()) return;
    matrix_mode = MATRIX_MODE_DRAW;
    matrix.drawPixel(x, y, color);
  }
}

void matrix_bitplan_data(uint8_t addr, uint8_t data) {
  if (has_matrix) {
    I2cGuard i2c;
    if (!core_owns_matrix()) return;
    matrix_mode = MATRIX_MODE_DRAW;
    // todo:
    // addr 0-7:   matrix1, red
    // addr 8-15:  matrix1, green
    // addr 16-23: matrix2, red
    // addr 24-31: matrix2, green
    //matrix.drawBitmap()
  }
}

static const uint8_t led_colors[4] = {LED_OFF, LED_RED, LED_GREEN, LED_YELLOW}; // by KG_MX_*

// a 16x8 picture of KG_MX_* colors
static void matrix_draw_fb(const uint8_t fb[KG_MATRIX_H][KG_MATRIX_W]) {
  matrix.clear();
  for (uint8_t y=0; y<8; y++) {
    for (uint8_t x=0; x<16; x++) {
      if (fb[y][x]) matrix.drawPixel(x, y, led_colors[fb[y][x] & 3]);
    }
  }
  matrix.writeDisplay();
}

// the preview of the meters in Settings: beats of random strength, each channel on its own
static void matrix_demo_signal() {
  volatile uint16_t *ch[2] = { &audio_l, &audio_r };
  for (uint8_t i = 0; i < 2; i++) {
    if (demo_wait[i] > 0) { demo_wait[i]--; continue; }
    demo_rnd ^= demo_rnd << 13; demo_rnd ^= demo_rnd >> 17; demo_rnd ^= demo_rnd << 5;
    uint16_t v = 1500 + demo_rnd % 24000;
    if (v > *ch[i]) *ch[i] = v;
    demo_wait[i] = 8 + (demo_rnd >> 16) % 24;
  }
}

void matrix_draw_audio() {
  if (preview == MATRIX_PREVIEW_METER) matrix_demo_signal();
  static uint8_t fb[KG_MATRIX_H][KG_MATRIX_W];
  kg_meter_draw(&meter, &mx_style, kg_meter_level(audio_l), kg_meter_level(audio_r), fb);
  matrix_draw_fb(fb);
  audio_l = kg_meter_decay(audio_l, mx_style.opt[KG_MX_OPT_METER_DECAY]);
  audio_r = kg_meter_decay(audio_r, mx_style.opt[KG_MX_OPT_METER_DECAY]);
}

// the text of the next pass, with {time} etc. of now
static void scroll_pass_start() {
  kg_matrix_vars_t vars = {cfg_name, cfg_build, zxrtc.getHour(), zxrtc.getMinute(), zxrtc.getDay(), zxrtc.getMonth(), (uint16_t) (2000 + zxrtc.getYear())};
  kg_matrix_expand(scroll_src, &vars, scroll_text, sizeof(scroll_text));
  scroll_width = strlen(scroll_text) * 6; // 6 pixels per character width
  scroll_pos = 16;
}

// one pixel to the left; true after the last pixel of a pass
static bool matrix_scroll_text()
{
  text_canvas.fillScreen(0);
  text_canvas.setCursor(scroll_pos, 0);
  text_canvas.print(scroll_text);
  matrix.clear();
  for (uint8_t y=0; y<8; y++) {
    for (uint8_t x=0; x<16; x++) {
      if (text_canvas.getPixel(x, y)) {
        int ch = (x - scroll_pos) / 6;
        matrix.drawPixel(x, y, led_colors[kg_matrix_text_color(scroll_color, x, ch)]);
      }
    }
  }
  matrix.writeDisplay();
  if (scroll_pos >= -scroll_width) {
    scroll_pos--;
    return false;
  }
  return true;
}

static void matrix_draw_image() {
  matrix.clear();
  for (uint8_t y=0; y<8; y++) {
    for (uint8_t x=0; x<16; x++) {
      matrix.drawPixel(x, y, led_colors[kg_matrix_frame_pixel(cfg.frames[image_frame], x, y)]);
    }
  }
  matrix.writeDisplay();
}

static void matrix_draw_fx() {
  static matrix_fx_input_t in;
  in.hour = zxrtc.getHour();
  in.minute = zxrtc.getMinute();
  in.second = zxrtc.getSecond();
  in.style = &mx_style;
  if (fx.effect == KG_MATRIX_FX_DRIVE) memcpy(in.drive, drives, sizeof(drives));
  matrix_fx_step(&fx, &in);
  matrix_draw_fb(fx.fb);
}

static void matrix_start(uint8_t mode);

// a pass of the scroll / a loop of the animation is over: after `repeat` of them, `then`
static void matrix_pass_done(uint8_t repeat) {
  passes++;
  if (repeat > 0 && passes >= repeat) {
    matrix_start(cfg.then);
  }
}

// start a mode of the settings (KG_MATRIX_MODE_*); with the I2C lock held
static void matrix_start_mode(uint8_t mode);

static void matrix_start_audio() {
  matrix_mode = MATRIX_MODE_AUDIO;
  kg_meter_init(&meter);
}

// the plugin whose options Settings edits, instead of the mode of the core
static void matrix_start_preview() {
  matrix.clear();
  matrix.writeDisplay();
  passes = 0;
  if (preview == MATRIX_PREVIEW_CLOCK) {
    matrix_fx_init(&fx, KG_MATRIX_FX_CLOCK, KG_MX_OFF, time_us_32());
    step_frames = matrix_fx_default_speed(KG_MATRIX_FX_CLOCK);
    step_count = step_frames - 1;
    matrix_mode = MATRIX_MODE_EFFECT;
  } else {
    audio_l = audio_r = 0;
    matrix_start_audio();
  }
}

// the matrix switched off in karabas.ini: dark in every mode
static void matrix_start(uint8_t mode) {
  if (mx_enabled && preview != MATRIX_PREVIEW_NONE) matrix_start_preview();
  else matrix_start_mode(mx_enabled ? mode : KG_MATRIX_MODE_BLANK);
}

static void matrix_start_mode(uint8_t mode) {
  matrix.clear();
  matrix.writeDisplay();
  passes = 0;
  step_count = 0;
  switch (mode) {
    case KG_MATRIX_MODE_SCROLL:
      scroll_src = cfg.text;
      scroll_color = cfg.color;
      scroll_repeat = cfg.repeat;
      step_frames = cfg.speed ? cfg.speed : MATRIX_SCROLL_FRAMES;
      scroll_pass_start();
      matrix_mode = MATRIX_MODE_SCROLL;
      break;
    case KG_MATRIX_MODE_EFFECT:
      matrix_fx_init(&fx, cfg.effect, cfg.color < KG_MATRIX_COLOR_RAINBOW ? cfg.color : KG_MX_OFF, time_us_32());
      step_frames = cfg.speed ? cfg.speed : matrix_fx_default_speed(cfg.effect);
      step_count = step_frames - 1; // the first step at the next frame
      matrix_mode = MATRIX_MODE_EFFECT;
      break;
    case KG_MATRIX_MODE_IMAGE:
      image_frame = 0;
      step_frames = cfg.speed ? cfg.speed : MATRIX_IMAGE_FRAMES;
      matrix_draw_image();
      matrix_mode = MATRIX_MODE_IMAGE;
      break;
    case KG_MATRIX_MODE_BLANK:
      matrix_mode = MATRIX_MODE_BLANK;
      break;
    default: // core
      if (cfg_core_type == CORE_TYPE_BOOT) {
        scroll_src = matrix_boot_text;
        scroll_color = KG_MATRIX_COLOR_DEFAULT;
        scroll_repeat = 0;
        step_frames = MATRIX_SCROLL_FRAMES;
        scroll_pass_start();
        matrix_mode = MATRIX_MODE_SCROLL;
      } else {
        matrix_start_audio();
        audio_l = 32000;
        audio_r = 32000;
      }
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

  // until the first core is loaded: the boot scroll
  cfg.brightness = KG_MATRIX_DEFAULT_BRIGHTNESS;
  cfg_core_type = CORE_TYPE_BOOT;
  kg_mx_style_default(&mx_style);
  matrix_init();
  if (has_matrix) matrix_start_mode(KG_MATRIX_MODE_CORE); // karabas.ini is not read yet
}

void matrix_register_handlers() {
  link_on(CMD_MATRIX_CTL, matrix_ctl_data);
  link_on(CMD_MATRIX_PIXEL, [](uint8_t addr, uint8_t data) { matrix_pixel_data(addr & 0x0F, addr >> 4, data & 0x03); });
  link_on(CMD_MATRIX_BITPLAN, [](uint8_t addr, uint8_t data) { matrix_bitplan_data(addr & 0x1F, data); });
  link_on(CMD_AUDIO_PEAKS_L, [](uint8_t addr, uint8_t data) { uint16_t new_audio = ((uint16_t)(addr) << 8) | (uint16_t)data; if (new_audio > audio_l) audio_l = new_audio; });
  link_on(CMD_AUDIO_PEAKS_R, [](uint8_t addr, uint8_t data) { uint16_t new_audio = ((uint16_t)(addr) << 8) | (uint16_t)data; if (new_audio > audio_r) audio_r = new_audio; });
}

void matrix_progress(uint32_t val, uint32_t max) {
  if (has_matrix && mx_enabled) {
    I2cGuard i2c;
    matrix.drawLine(0, 0, map(val, 0, max, 0, 15), 0, LED_GREEN);
    matrix.drawLine(0, 1, map(val, 0, max, 0, 15), 1, LED_GREEN);
    matrix.writeDisplay();
  }
}

void matrix_set_config(const kg_matrix_cfg_t *c, const char *name, const char *build) {
  I2cGuard i2c;
  cfg = *c;
  strlcpy(cfg_name, name, sizeof(cfg_name));
  strlcpy(cfg_build, build, sizeof(cfg_build));
}

void matrix_drive_mount(uint8_t slot, uint32_t sectors) {
  if (slot >= MATRIX_FX_DRIVES) return;
  drives[slot].sectors = sectors;
  drives[slot].lba = 0;
}

void matrix_drive_access(uint8_t slot, uint32_t lba, bool write) {
  if (slot >= MATRIX_FX_DRIVES) return;
  drives[slot].lba = lba;
  if (write) drives[slot].writes++;
  else drives[slot].reads++;
}

void matrix_setup(const setup_t &s) {
  if (!has_matrix) return;
  I2cGuard i2c;
  bool restart = s.matrix_enabled != mx_enabled;
  mx_enabled = s.matrix_enabled;
  mx_brightness = s.matrix_brightness;
  mx_style = s.matrix_style; // the next frame draws with it
  if (matrix_mode == MATRIX_MODE_ICON || matrix_mode == MATRIX_MODE_LOADING) return;
  matrix_init();
  if (restart) matrix_start(cfg.mode);
}

void matrix_preview(uint8_t what) {
  if (!has_matrix) return;
  I2cGuard i2c;
  if (what == preview) return;
  preview = what;
  if (matrix_mode == MATRIX_MODE_ICON || matrix_mode == MATRIX_MODE_LOADING) return;
  matrix_start(cfg.mode);
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
    mx_enabled = hw_setup.matrix_enabled;
    mx_brightness = hw_setup.matrix_brightness;
    mx_style = hw_setup.matrix_style;
    preview = MATRIX_PREVIEW_NONE;
    matrix_init();
    cfg_core_type = core_type;
    matrix_start(cfg.mode);
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
  if (!has_matrix || !mx_enabled || icon >= sizeof(matrix_icons) / sizeof(matrix_icons[0])) return;
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

// one display frame, called every MATRIX_FRAME_MS (the steps of the modes and the button poll
// are counted in frames, so they stay evenly spaced however the frames are scheduled)
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
        if (++step_count >= step_frames) {
          step_count = 0;
          if (matrix_scroll_text()) {
            scroll_pass_start();
            matrix_pass_done(scroll_repeat);
          }
        }
        break;
      case MATRIX_MODE_EFFECT:
        if (++step_count >= step_frames) {
          step_count = 0;
          matrix_draw_fx();
        }
        break;
      case MATRIX_MODE_IMAGE:
        if (++step_count >= step_frames) {
          step_count = 0;
          image_frame = (image_frame + 1) % cfg.frame_count;
          if (cfg.frame_count > 1) matrix_draw_image();
          if (image_frame == 0) matrix_pass_done(cfg.repeat);
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
