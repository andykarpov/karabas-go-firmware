#include "config.h"
#include "types.h"
#include "main.h"
#include "ft812.h"
#include "ui/bitmaps.h"
#include "osd.h"
#include "input/sega.h"
#include "input/usb_hid_keys.h"
#include "apps/app_core_browser.h"
#include "apps/app_setup.h"
#include "ui/sorts.h"
#include "file_name.h"
#include "ui/ft_mouse.h"
#include <stdio.h>

core_list_item_t cores[MAX_CORES];
uint8_t cores_len = 0;
uint8_t core_sel = 0;
const uint8_t core_page_size = MAX_CORES_PER_PAGE;
// the graphic list: as many buttons as fit between the logo and the page line
#define FT_CORE_TOP (64 + 16 + 8)
#define FT_CORE_STEP 40
#define FT_CORE_BOTTOM 60 // the page line and the footer
uint8_t core_pages = 1;
uint8_t core_page = 1;

static uint8_t ft_core_page_size() {
  int n = (ft_height() - FT_CORE_BOTTOM - FT_CORE_TOP) / FT_CORE_STEP;
  return n < 1 ? 1 : n;
}

// the cores on a page of the menu on screen (Left / Right jump by it)
static uint8_t page_size() { return has_ft ? ft_core_page_size() : core_page_size; }
uint16_t rot = 0;
uint32_t autoload_timer = 0;
bool autoload_enabled;
uint32_t autoload_countdown;

// the clickable areas of the graphic browser besides the cores (0..cores_len-1)
enum { HIT_SETUP = 1000, HIT_PREV, HIT_NEXT };
static int16_t hover = FT_HIT_NONE; // under the pointer at the last draw
static bool setup_focus = false;    // the graphic browser: the keys are on the Setup button

void app_core_browser_overlay() {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_clear();
  osd_header(core.build, core.id, HW_ID);
  print_time();
  osd_set_pos(0,5);
  app_core_browser_menu(APP_COREBROWSER_MENU_OFFSET);  
  // footer
  osd_line(21);
  osd_set_pos(0, 22); 
  osd_set_color(OSD_COLOR_CYAN_I, OSD_COLOR_FLASH); osd_print("S");
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK); osd_print("etup");
  osd_set_pos(29, 22); // right of the page number
  osd_set_color(OSD_COLOR_CYAN_I, OSD_COLOR_FLASH); osd_print("G");
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK); osd_print("UI");
  osd_line(23);
  osd_set_pos(1,24); osd_print("Please use arrows to navigate");
  osd_set_pos(1,25); osd_print("Press Enter to load selection");
  osd_update();
}

void app_core_browser_menu(uint8_t vpos) {
  core_pages = ceil((float)cores_len / core_page_size);
  core_page = ceil((float)(core_sel+1)/core_page_size);
  uint8_t core_from = (core_page-1)*core_page_size;
  uint8_t core_to = core_page*core_page_size > cores_len ? cores_len : core_page*core_page_size;
  uint8_t core_fill = core_page*core_page_size;
  uint8_t pos = vpos;
  for(uint8_t i=core_from; i < core_to; i++) {
    osd_set_pos(0, pos);
    if (core_sel == i) {
      osd_set_color(OSD_COLOR_BLACK, OSD_COLOR_WHITE);
    } else {
      osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
    }
    char name[18]; memcpy(name, cores[i].name, 17); name[17] = '\0';
    char b[40];
    sprintf(b, "%-3d ", i+1); 
    osd_print(b);
    osd_print(name);
    osd_print(cores[i].build);
    osd_print(cores[i].flash ? "  F" : " SD");
    pos++;
  }
  if (core_fill > core_to) {
    for (uint8_t i=core_to; i<core_fill; i++) {
      osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
      for (uint8_t j=0; j<32; j++) {
        osd_print(" ");
      }
    }
  }
  osd_set_pos(8, vpos + core_page_size + 1); osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  char b[40];
  sprintf(b, "Page %02d of %02d", core_page, core_pages); 
  osd_print(b);
  osd_update();
}

// The background gradient as a bitmap in GRAM, after the kept frame of ft_mouse (0xF0400 + 8 KB):
// CMD_GRADIENT took too much of the render time of every line (in 800x600, where the system
// clock is the pixel clock, busy lines came out unfinished). A 4 px wide column, ordered dither for the RGB565 steps,
// repeated across the screen: one bitmap a line. Made again when the colors or the mode change,
// into the other of two buffers: the frame on the screen keeps drawing the old one until the swap
// (a theme change tore the background for a frame).
#define GRADIENT_OFFSET 0xF4000
#define GRADIENT_W 4
#define GRADIENT_MAX_H 450 // 3/4 of 600 lines
#define GRADIENT_BUF 0x1000 // the second buffer is after the first
static_assert(GRADIENT_W * 2 * GRADIENT_MAX_H <= GRADIENT_BUF, "the gradient buffers overlap");
static_assert(GRADIENT_OFFSET + 2 * GRADIENT_BUF <= 0x100000, "the gradient is past the GRAM");

static uint16_t gradient_px[GRADIENT_W * GRADIENT_MAX_H];
static uint32_t gradient_from, gradient_to;
static uint16_t gradient_h; // 0: not in GRAM
static uint8_t gradient_buf; // the buffer of the last gradient

static uint8_t gradient_mix(uint32_t from, uint32_t to, uint8_t shift, uint16_t y, uint16_t h) {
  int a = (from >> shift) & 0xFF, b = (to >> shift) & 0xFF;
  return a + (b - a) * y / (h - 1);
}

static void gradient_load(uint32_t from, uint32_t to, uint16_t h) {
  static const uint8_t bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
  for (uint16_t y = 0; y < h; y++) {
    uint8_t r = gradient_mix(from, to, 16, y, h), g = gradient_mix(from, to, 8, y, h), b = gradient_mix(from, to, 0, y, h);
    for (uint8_t x = 0; x < GRADIENT_W; x++) {
      uint8_t t = bayer[y & 3][x];
      uint16_t r5 = (r + t / 2) >> 3, g6 = (g + t / 4) >> 2, b5 = (b + t / 2) >> 3;
      if (r5 > 31) r5 = 31;
      if (g6 > 63) g6 = 63;
      if (b5 > 31) b5 = 31;
      gradient_px[y * GRADIENT_W + x] = (r5 << 11) | (g6 << 5) | b5;
    }
  }
  gradient_buf ^= 1;
  ft_write_gram(GRADIENT_OFFSET + gradient_buf * GRADIENT_BUF, GRADIENT_W * 2 * h, (const uint8_t *) gradient_px);
  gradient_from = from;
  gradient_to = to;
  gradient_h = h;
}

static_assert(KEY1_PCM_START + KEY1_PCM_SIZE <= sizeof(key1Data), "sound 1 is past its array");
static_assert(KEY2_PCM_START + KEY2_PCM_SIZE <= sizeof(key2Data), "sound 2 is past its array");
static_assert(KEY3_PCM_START + KEY3_PCM_SIZE <= sizeof(key3Data), "sound 3 is past its array");
static_assert(KEY4_PCM_START + KEY4_PCM_SIZE <= sizeof(key4Data), "sound 4 is past its array");

void app_core_browser_ft_load() {
  gradient_h = 0; // the GRAM is cleared by a reset of the FT812
  ft_set_sound(FT81x_SOUND_COWBELL, 60);
  ft_load_image(LOGO_OFFSET, LOGO_SIZE, logoData); // karabas logo, starts from 0 in GRAM
  ft_load_image(BG_OFFSET, BG_SIZE, bgData); // karabas bg, starts after logo in GRAM
  ft_write_gram(KEY1_OFFSET, KEY1_PCM_SIZE, key1Data + KEY1_PCM_START); // pcm sound, starts from after logo and bg in GRAM
  ft_write_gram(KEY2_OFFSET, KEY2_PCM_SIZE, key2Data + KEY2_PCM_START); // pcm sound, starts from after logo and bg in GRAM
  ft_write_gram(KEY3_OFFSET, KEY3_PCM_SIZE, key3Data + KEY3_PCM_START); // pcm sound, starts from after logo and bg in GRAM
  ft_write_gram(KEY4_OFFSET, KEY4_PCM_SIZE, key4Data + KEY4_PCM_START); // pcm sound, starts from after logo and bg in GRAM
  ft_mouse_load();
}

void app_core_browser_ft_overlay() {
  app_core_browser_ft_load();
  ft_mouse_reset();
  app_core_browser_ft_menu(0);
}

void app_core_browser_ft_background(const setup_t *t) {
  ft_clear(t->color_bg);
  // from the top to 3/4 of the height, as CMD_GRADIENT drew it; below is the clear color
  uint16_t h = ft_height() - ft_height() / 4;
  if (h > GRADIENT_MAX_H) h = GRADIENT_MAX_H;
  if (h != gradient_h || t->color_gradient != gradient_from || t->color_bg != gradient_to) {
    gradient_load(t->color_gradient, t->color_bg, h);
  }
  ft_draw_bitmap_rgb565_repeat_x(GRADIENT_OFFSET + gradient_buf * GRADIENT_BUF, 0, 0, GRADIENT_W, h, ft_width());
}

void app_core_browser_ft_decor(const setup_t *t, bool show_char) {
  if (t->ft_time) {
    char time[12];
    sprintf(time, "%02d:%02d:%02d", rtc_get_hour(), rtc_get_minute(), rtc_get_second());
    ft_draw_text(ft_width()-88, 30, 30, t->color_text, FT81x_OPT_CENTER, time);
  }
  if (t->ft_date) {
    char date[12];
    sprintf(date, "%02d/%02d/%02d", rtc_get_day(), rtc_get_month(), rtc_get_year() % 100);
    ft_draw_text(ft_width()-88, 60, 30, t->color_text, FT81x_OPT_CENTER, date);
  }
  ft_draw_bitmap(0, 8, 8, LOGO_WIDTH, LOGO_HEIGHT, 4, 0);  // logo scaled 4x
  if (t->ft_char && show_char) {
    ft_draw_bitmap(LOGO_BITMAP_SIZE, ft_width()-BG_WIDTH-8, ft_height()-BG_HEIGHT-8, BG_WIDTH, BG_HEIGHT, 1, 0); // bg image
  }
}

void app_core_browser_ft_sound(uint8_t sound, bool wait) {
  uint32_t key_size, key_samplerate, key_duration, key_offset;
  switch (sound) {
    case 2:
      key_size = KEY2_PCM_SIZE;
      key_samplerate = KEY2_SAMPLERATE;
      key_duration = KEY2_DURATION;
      key_offset = KEY2_OFFSET;
      break;
    case 3:
      key_size = KEY3_PCM_SIZE;
      key_samplerate = KEY3_SAMPLERATE;
      key_duration = KEY3_DURATION;
      key_offset = KEY3_OFFSET;
      break;
    case 4:
      key_size = KEY4_PCM_SIZE;
      key_samplerate = KEY4_SAMPLERATE;
      key_duration = KEY4_DURATION;
      key_offset = KEY4_OFFSET;
      break;
    default:
      key_size = KEY1_PCM_SIZE;
      key_samplerate = KEY1_SAMPLERATE;
      key_duration = KEY1_DURATION;
      key_offset = KEY1_OFFSET;
  }
  // a length that is not a multiple of 8 plays on past the sound (the last few samples go)
  ft_play_audio(key_offset, key_size & ~7u, key_samplerate, FT81x_AUDIO_FORMAT_ULAW, false);
  if (wait) {
    ms_delay(key_duration);
    ft_stop_audio(); // not stopSound(): that silences the click effect (REG_SOUND) instead
  }
}

void app_core_browser_ft_menu(uint8_t play_sounds) {

  ft_begin_display_list();

  uint32_t color_button = hw_setup.color_button;
  uint32_t color_button_active = hw_setup.color_active;
  uint32_t color_text = hw_setup.color_text;
  uint32_t color_text_active = hw_setup.color_text_active;
  uint32_t color_copyright = hw_setup.color_copyright;

  app_core_browser_ft_background(&hw_setup);
  ft_hit_clear();

  uint8_t ft_page = ft_core_page_size();
  core_pages = ceil((float)cores_len / ft_page);
  core_page = ceil((float)(core_sel+1)/ft_page);
  uint8_t core_from = (core_page-1)*ft_page;
  uint8_t core_to = core_page*ft_page > cores_len ? cores_len : core_page*ft_page;
  uint8_t pos = 0;
  uint8_t offset = FT_CORE_TOP;
  for(uint8_t i=core_from; i < core_to; i++) {
    char trimmed[sizeof(cores[0].name)], name[18];
    strcpy(trimmed, cores[i].name); kg_trim(trimmed);
    snprintf(name, sizeof(name), "%s", trimmed);
    const bool sel = i == core_sel && !setup_focus;
    const uint32_t colorb = sel ? color_button_active : color_button;
    const uint32_t colort = sel ? color_text_active : color_text;
    if (ft_hit_add(ft_width()/4 + 8, offset + pos*FT_CORE_STEP, ft_width()/2-16, 32, i)) {
      // hovered: a rect grows by its corner radius, so this is a 2 px frame around the button
      ft_draw_rect(ft_width()/4 + 8, offset + pos*FT_CORE_STEP, ft_width()/2-16, 32, 2, color_button_active);
    }
    ft_draw_button(ft_width()/4 + 8, offset + pos*FT_CORE_STEP, ft_width()/2-16, 32, 28, colort, colorb, (hw_setup.ft_3d_buttons) ? FT81x_OPT_3D : FT81x_OPT_FLAT , name);
    if (cores[i].flash) {
      ft_draw_text(ft_width()/4+ft_width()/2-16-24, offset + pos*FT_CORE_STEP + 16, 28, colort, FT81x_OPT_CENTERY, "F\0");
    }
    if (autoload_enabled && i==core_sel) {
      uint32_t diff = (ms_since(autoload_timer) < autoload_countdown * 1000) ? ceil((autoload_countdown * 1000 - ms_since(autoload_timer))/1000) : 0;
      ft_draw_gauge(ft_width()/4+16+12, offset + pos*FT_CORE_STEP + 16, 12, colort, colorb, 0, 1, 1, diff, autoload_countdown);
    }
    pos++;
  }

  char b[40]; 

  //ft_draw_text(112, ft_height()-40, 27, color_copyright, FT81x_OPT_CENTER, "www.karabas.uk\0");
  sprintf(b, "FW build: %s", STRINGIZE_VALUE_OF(BUILD_VER));
  ft_draw_text(112, ft_height()-40, 27, color_copyright, FT81x_OPT_CENTER, b);
  ft_draw_text(112, ft_height()-20, 27, color_copyright, FT81x_OPT_CENTER, "(c) 2026 andykarpov");
  sprintf(b, "Page %d of %d", core_page, core_pages);
  ft_draw_text(ft_width()/2, ft_height()-40, 27, color_copyright, FT81x_OPT_CENTER, b);
  if (core_pages > 1) {
    bool h = ft_hit_add(ft_width()/2 - 96, ft_height()-54, 32, 28, HIT_PREV);
    ft_draw_text(ft_width()/2 - 80, ft_height()-40, 28, h ? color_text : color_copyright, FT81x_OPT_CENTER, "<");
    h = ft_hit_add(ft_width()/2 + 64, ft_height()-54, 32, 28, HIT_NEXT);
    ft_draw_text(ft_width()/2 + 80, ft_height()-40, 28, h ? color_text : color_copyright, FT81x_OPT_CENTER, ">");
  }

  if (autoload_enabled) {
    uint32_t diff = (ms_since(autoload_timer) < autoload_countdown * 1000) ? ceil((autoload_countdown * 1000 - ms_since(autoload_timer))/1000) : 0;
    sprintf(b, "Autoloading core in %lu s", (unsigned long) diff);
    ft_draw_text(ft_width()/2, ft_height()-20, 27, color_copyright, FT81x_OPT_CENTER, b);
  } else {
    sprintf(b, "Autoload disabled");
    ft_draw_text(ft_width()/2, ft_height()-20, 27, color_copyright, FT81x_OPT_CENTER, b);
  }

  if (play_sounds == 1 && hw_setup.ft_click) {
      ft_play_sound();
  } else if (play_sounds == 2 && hw_setup.ft_sound > 0) {
    app_core_browser_ft_sound(hw_setup.ft_sound, true);
  }

  // the Setup button (S) under the logo
  if (ft_hit_add(14, 82, 120, 44, HIT_SETUP)) ft_draw_circle(36, 104, 20, color_button_active);
  ft_draw_circle(36, 104, 18, setup_focus ? color_button_active : color_button);
  ft_draw_text(36, 104, 29, setup_focus ? color_text_active : color_text, FT81x_OPT_CENTER, "S");
  ft_draw_text(62, 104, 28, color_text, FT81x_OPT_CENTERY, "Setup");

  app_core_browser_ft_decor(&hw_setup, true);

  hover = ft_hover();
  ft_frame_end();
}


// the header of the open core file (file1)
core_list_item_t app_core_browser_get_item(const char *filename) {
  core_list_item_t core;
  snprintf(core.filename, sizeof(core.filename), "%s", filename);
  core.flash = false;
  file_seek(FILE_POS_CORE_ID); file_read_bytes(core.id, 32); core.id[32] = '\0';
  file_seek(FILE_POS_CORE_NAME); file_read_bytes(core.name, 32); core.name[32] = '\0';
  uint8_t visible; file_seek(FILE_POS_CORE_VISIBLE); visible = file_read(); core.visible = (visible > 0);
  file_seek(FILE_POS_CORE_ORDER); core.order = file_read();
  file_seek(FILE_POS_CORE_TYPE); core.type = file_read();
  file_seek(FILE_POS_CORE_BUILD); file_read_bytes(core.build, 8); core.build[8] = '\0';
  return core;
}

void app_core_browser_read_list() {

  // files from sd card
  if (has_sd) {
    if (!sd_opendir(&root1, "/")) {
      return;
    }
    FILINFO fi;
    while (f_readdir(&root1, &fi) == FR_OK && fi.fname[0]) {
      if (!(fi.fattrib & AM_DIR) && strlen(fi.fname) < 32 && kg_name_has_ext(fi.fname, CORE_EXT) &&
          sd_open(&file1, fi.fname, FA_READ)) {
        cores[cores_len] = app_core_browser_get_item(fi.fname);
        cores_len++;
        sd_close(&file1);
      }
    }
  }
  // sort by core order number
  cores_sort(cores, cores_len);
  
  autoload_enabled = false;
  // pre-select autoload core
  for (uint8_t i=0; i<cores_len; i++) {
    char s1[sizeof(hw_setup.autoload_core)], s2[sizeof(cores[0].id)];
    strcpy(s1, hw_setup.autoload_core); kg_trim(s1);
    strcpy(s2, cores[i].id); kg_trim(s2);
    if (!autoload_enabled && hw_setup.autoload_enabled && strcmp(s2, s1) == 0) {
      autoload_enabled = true;
      autoload_countdown = hw_setup.autoload_timeout;
      core_sel = i;
      autoload_timer = ms_now();
    }
  }

}

static void start_core() {
  autoload_enabled = false;
  if (has_ft) {
    app_core_browser_ft_menu(2); // play wav
  }
  d_printf("Selected core %s to boot from menu", cores[core_sel].filename); d_println();
  char buf[sizeof(cores[0].filename)];
  strcpy(buf, cores[core_sel].filename); kg_trim(buf);
  has_ft = false;
  do_configure(buf);
  switch (core.type) {
    case CORE_TYPE_BOOT: osd_state = state_core_browser; break;
    case CORE_TYPE_OSD: osd_state = state_main; break;
    case CORE_TYPE_FILELOADER: osd_state = state_file_loader; break;
    default: osd_state = state_main;
  }
}

// the graphic setup on the FT812, else the classic one
static void open_setup() {
  autoload_enabled = false;
  setup_focus = false;
  osd_return_state = state_core_browser;
  osd_state = has_ft ? state_ft_setup : state_setup;
}

// the graphic browser with the keys on the Setup button: Enter opens it, Right / Up / Down go
// back to the cores. true if the key was taken.
static bool setup_focus_key(uint8_t key) {
  if (key == KEY_ENTER) {
    open_setup();
    return true;
  }
  if (key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN) {
    setup_focus = false;
    app_core_browser_ft_menu(1);
    return true;
  }
  return false;
}

void app_core_browser_on_keyboard() {
      uint8_t key = ui_nav_key();
      if (has_ft) ft_mouse_hide(); // the keys move the selection
      if (!has_ft) setup_focus = false;
      if (setup_focus && setup_focus_key(key)) return;
      if (cores_len > 0) {
        if (key == KEY_LEFT && has_ft && core_sel == 0) {
          // already on the first core: onto the Setup button (not by a held Left)
          autoload_enabled = false;
          if (!ui_repeat) setup_focus = true;
        } else {
          // up / down, left / right a page
          uint16_t sel = core_sel;
          if (ui_list_key(&sel, cores_len, page_size(), key)) autoload_enabled = false;
          core_sel = sel;
        }

        if (key == KEY_ENTER) {
          start_core();
        }

        // enter setup mode (S, joy start)
        if (key == KEY_S || ((joyL | joyR) & SC_BTN_START)) {
          open_setup();
        }

        // redraw core browser on keypress
        if (osd_state == state_core_browser) {
          if (has_ft) {
            app_core_browser_ft_menu(1);
          } else {
            app_core_browser_menu(APP_COREBROWSER_MENU_OFFSET);
          }
        }
      }

      // return back to classic osd
      if (key == KEY_SPACE && has_ft) {
        has_ft = false;
        ft_vga(false);
        ft_spi(false);
        app_core_browser_overlay();
      }

      // the graphic menu, also when it is off in karabas.ini (until the next start)
      if (key == KEY_G && !has_ft) {
        autoload_enabled = false;
        ft_spi(true);
        has_ft = ft_init(hw_setup.ft_video_mode);
        if (has_ft) {
          d_printf("FT81x started (system clock %lu kHz), switching to FT OSD", (unsigned long) (ft_init_clock_hz() / 1000)); d_println();
          ft_vga(true);
          app_core_browser_ft_overlay();
        } else {
          d_println("FT81x did not come up");
          ft_spi(false);
        }
      }
}

// the mouse on the graphic browser: the pointer hovers, a click selects a core, a click on the
// selected one starts it; the wheel moves the selection
void app_core_browser_tick() {
  if (!has_ft) return;
  ft_mouse_event_t ev;
  if (!ft_mouse_poll(&ev)) return;
  bool act = ev.pressed || ev.wheel;
  if (act) autoload_enabled = false;
  if (act) setup_focus = false; // the mouse picks
  if (ev.wheel && cores_len > 0) {
    int sel = core_sel - ev.wheel;
    core_sel = sel < 0 ? 0 : sel >= cores_len ? cores_len - 1 : sel;
  }
  if (ev.pressed & FT_MOUSE_LEFT) {
    int16_t id = ft_hover();
    if (id >= 0 && id < cores_len) {
      if (id == core_sel) {
        start_core();
        return;
      }
      core_sel = id;
    } else if (id == HIT_SETUP) {
      open_setup();
      return;
    } else if (id == HIT_PREV) {
      uint8_t n = ft_core_page_size();
      core_sel = core_sel >= n ? (core_sel / n - 1) * n : 0;
    } else if (id == HIT_NEXT) {
      uint8_t n = ft_core_page_size();
      uint8_t next = (core_sel / n + 1) * n;
      if (next < cores_len) core_sel = next;
    }
  }
  if (act || ft_hover() != hover) {
    app_core_browser_ft_menu(act && hw_setup.ft_click ? 1 : 0);
  } else if (ev.moved) {
    ft_pointer_frame();
  }
}

void app_core_browser_on_time() {
  if (autoload_enabled) {
    if (ms_since(autoload_timer) > autoload_countdown * 1000) {
      autoload_enabled = false;
      char buf[sizeof(cores[0].filename)];
      strcpy(buf, cores[core_sel].filename); kg_trim(buf);
      has_ft = false;
      do_configure(buf);
      switch (core.type) {
        case CORE_TYPE_BOOT: osd_state = state_core_browser; break;
        case CORE_TYPE_OSD: osd_state = state_main; break;
        case CORE_TYPE_FILELOADER: osd_state = state_file_loader; break;
        default: osd_state = state_main;
      }
    }
  }
}
