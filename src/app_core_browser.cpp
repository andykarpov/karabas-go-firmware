#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "FT812.h"
#include "bitmaps.h"
#include "OSD.h"
#include "SdFat.h"
#include "SegaController.h"
#include "usb_hid_keys.h"
#include <algorithm>
#include <tuple>
#include "app_core_browser.h"
#include "app_setup.h"
#include "sorts.h"
#include "file_name.h"
#include "ui/ft_mouse.h"
#include <cstdio>
#include <iostream>
using namespace std;

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
  int n = (ft.height() - FT_CORE_BOTTOM - FT_CORE_TOP) / FT_CORE_STEP;
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
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();
  zxosd.header(core.build, core.id, HW_ID);
  print_time();
  zxosd.setPos(0,5);
  app_core_browser_menu(APP_COREBROWSER_MENU_OFFSET);  
  // footer
  zxosd.line(21);
  zxosd.setPos(0, 22); 
  zxosd.setColor(OSD::COLOR_CYAN_I, OSD::COLOR_FLASH); zxosd.print("S");
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK); zxosd.print("etup");
  zxosd.setPos(29, 22); // right of the page number
  zxosd.setColor(OSD::COLOR_CYAN_I, OSD::COLOR_FLASH); zxosd.print("G");
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK); zxosd.print("UI");
  zxosd.line(23);
  zxosd.setPos(1,24); zxosd.print("Please use arrows to navigate");
  zxosd.setPos(1,25); zxosd.print("Press Enter to load selection");
  zxosd.update();
}

void app_core_browser_menu(uint8_t vpos) {
  core_pages = ceil((float)cores_len / core_page_size);
  core_page = ceil((float)(core_sel+1)/core_page_size);
  uint8_t core_from = (core_page-1)*core_page_size;
  uint8_t core_to = core_page*core_page_size > cores_len ? cores_len : core_page*core_page_size;
  uint8_t core_fill = core_page*core_page_size;
  uint8_t pos = vpos;
  for(uint8_t i=core_from; i < core_to; i++) {
    zxosd.setPos(0, pos);
    if (core_sel == i) {
      zxosd.setColor(OSD::COLOR_BLACK, OSD::COLOR_WHITE);
    } else {
      zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
    }
    char name[18]; memcpy(name, cores[i].name, 17); name[17] = '\0';
    char b[40];
    sprintf(b, "%-3d ", i+1); 
    zxosd.print(b);
    zxosd.print(name);
    zxosd.print(cores[i].build);
    zxosd.print(cores[i].flash ? "  F" : " SD");
    pos++;
  }
  if (core_fill > core_to) {
    for (uint8_t i=core_to; i<core_fill; i++) {
      zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
      for (uint8_t j=0; j<32; j++) {
        zxosd.print(" ");
      }
    }
  }
  zxosd.setPos(8, vpos + core_page_size + 1); zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  char b[40];
  sprintf(b, "Page %02d of %02d", core_page, core_pages); 
  zxosd.print(b);
  zxosd.update();
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
  ft.writeGRAM(GRADIENT_OFFSET + gradient_buf * GRADIENT_BUF, GRADIENT_W * 2 * h, (const uint8_t *) gradient_px, false);
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
  ft.setSound(FT81x_SOUND_COWBELL, 60);
  ft.loadImage(LOGO_OFFSET, LOGO_SIZE, logoData, false); // karabas logo, starts from 0 in GRAM
  ft.loadImage(BG_OFFSET, BG_SIZE, bgData, false); // karabas bg, starts after logo in GRAM
  ft.writeGRAM(KEY1_OFFSET, KEY1_PCM_SIZE, key1Data + KEY1_PCM_START); // pcm sound, starts from after logo and bg in GRAM
  ft.writeGRAM(KEY2_OFFSET, KEY2_PCM_SIZE, key2Data + KEY2_PCM_START); // pcm sound, starts from after logo and bg in GRAM
  ft.writeGRAM(KEY3_OFFSET, KEY3_PCM_SIZE, key3Data + KEY3_PCM_START); // pcm sound, starts from after logo and bg in GRAM
  ft.writeGRAM(KEY4_OFFSET, KEY4_PCM_SIZE, key4Data + KEY4_PCM_START); // pcm sound, starts from after logo and bg in GRAM
  ft_mouse_load();
}

void app_core_browser_ft_overlay() {
  app_core_browser_ft_load();
  ft_mouse_reset();
  app_core_browser_ft_menu(0);
}

void app_core_browser_ft_background(const setup_t &t) {
  ft.clear(t.color_bg);
  // from the top to 3/4 of the height, as CMD_GRADIENT drew it; below is the clear color
  uint16_t h = ft.height() - ft.height() / 4;
  if (h > GRADIENT_MAX_H) h = GRADIENT_MAX_H;
  if (h != gradient_h || t.color_gradient != gradient_from || t.color_bg != gradient_to) {
    gradient_load(t.color_gradient, t.color_bg, h);
  }
  ft.drawBitmapRGB565RepeatX(GRADIENT_OFFSET + gradient_buf * GRADIENT_BUF, 0, 0, GRADIENT_W, h, ft.width());
}

void app_core_browser_ft_decor(const setup_t &t, bool show_char) {
  if (t.ft_time) {
    char time[9];
    sprintf(time, "%02d:%02d:%02d", zxrtc.getHour(), zxrtc.getMinute(), zxrtc.getSecond());
    ft.drawText(ft.width()-88, 30, 30, t.color_text, FT81x_OPT_CENTER, time);
  }
  if (t.ft_date) {
    char date[9];
    sprintf(date, "%02d/%02d/%02d", zxrtc.getDay(), zxrtc.getMonth(), zxrtc.getYear() % 100);
    ft.drawText(ft.width()-88, 60, 30, t.color_text, FT81x_OPT_CENTER, date);
  }
  ft.drawBitmap(0, 8, 8, LOGO_WIDTH, LOGO_HEIGHT, 4, 0);  // logo scaled 4x
  if (t.ft_char && show_char) {
    ft.overlayBitmap(LOGO_BITMAP_SIZE, ft.width()-BG_WIDTH-8, ft.height()-BG_HEIGHT-8, BG_WIDTH, BG_HEIGHT, 1, 0); // bg image
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
  ft.playAudio(key_offset, key_size & ~7u, key_samplerate, FT81x_AUDIO_FORMAT_ULAW, false);
  if (wait) {
    delay(key_duration);
    ft.stopAudio(); // not stopSound(): that silences the click effect (REG_SOUND) instead
  }
}

void app_core_browser_ft_menu(uint8_t play_sounds) {

  ft.beginDisplayList();

  uint32_t color_button = hw_setup.color_button;
  uint32_t color_button_active = hw_setup.color_active;
  uint32_t color_text = hw_setup.color_text;
  uint32_t color_text_active = hw_setup.color_text_active;
  uint32_t color_copyright = hw_setup.color_copyright;

  app_core_browser_ft_background(hw_setup);
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
    if (ft_hit_add(ft.width()/4 + 8, offset + pos*FT_CORE_STEP, ft.width()/2-16, 32, i)) {
      // hovered: a rect grows by its corner radius, so this is a 2 px frame around the button
      ft.drawRect(ft.width()/4 + 8, offset + pos*FT_CORE_STEP, ft.width()/2-16, 32, 2, color_button_active);
    }
    ft.drawButton(ft.width()/4 + 8, offset + pos*FT_CORE_STEP, ft.width()/2-16, 32, 28, colort, colorb, (hw_setup.ft_3d_buttons) ? FT81x_OPT_3D : FT81x_OPT_FLAT , name);
    if (cores[i].flash) {
      ft.drawText(ft.width()/4+ft.width()/2-16-24, offset + pos*FT_CORE_STEP + 16, 28, colort, FT81x_OPT_CENTERY, "F\0");
    }
    if (autoload_enabled && i==core_sel) {
      uint32_t diff = (ms_since(autoload_timer) < autoload_countdown * 1000) ? ceil((autoload_countdown * 1000 - ms_since(autoload_timer))/1000) : 0;
      ft.drawGauge(ft.width()/4+16+12, offset + pos*FT_CORE_STEP + 16, 12, colort, colorb, 0, 1, 1, diff, autoload_countdown);
    }
    pos++;
  }

  char b[40]; 

  //ft.drawText(112, ft.height()-40, 27, color_copyright, FT81x_OPT_CENTER, "www.karabas.uk\0");
  sprintf(b, "FW build: %s\0", STRINGIZE_VALUE_OF(BUILD_VER));
  ft.drawText(112, ft.height()-40, 27, color_copyright, FT81x_OPT_CENTER, b);
  ft.drawText(112, ft.height()-20, 27, color_copyright, FT81x_OPT_CENTER, "(c) 2026 andykarpov\0");
  sprintf(b, "Page %d of %d\0", core_page, core_pages);
  ft.drawText(ft.width()/2, ft.height()-40, 27, color_copyright, FT81x_OPT_CENTER, b);
  if (core_pages > 1) {
    bool h = ft_hit_add(ft.width()/2 - 96, ft.height()-54, 32, 28, HIT_PREV);
    ft.drawText(ft.width()/2 - 80, ft.height()-40, 28, h ? color_text : color_copyright, FT81x_OPT_CENTER, "<");
    h = ft_hit_add(ft.width()/2 + 64, ft.height()-54, 32, 28, HIT_NEXT);
    ft.drawText(ft.width()/2 + 80, ft.height()-40, 28, h ? color_text : color_copyright, FT81x_OPT_CENTER, ">");
  }

  if (autoload_enabled) {
    uint32_t diff = (ms_since(autoload_timer) < autoload_countdown * 1000) ? ceil((autoload_countdown * 1000 - ms_since(autoload_timer))/1000) : 0;
    sprintf(b, "Autoloading core in %d s\0", diff);
    ft.drawText(ft.width()/2, ft.height()-20, 27, color_copyright, FT81x_OPT_CENTER, b);
  } else {
    sprintf(b, "Autoload disabled\0");
    ft.drawText(ft.width()/2, ft.height()-20, 27, color_copyright, FT81x_OPT_CENTER, b);
  }

  if (play_sounds == 1 && hw_setup.ft_click) {
      ft.playSound();
  } else if (play_sounds == 2 && hw_setup.ft_sound > 0) {
    app_core_browser_ft_sound(hw_setup.ft_sound, true);
  }

  // the Setup button (S) under the logo
  if (ft_hit_add(14, 82, 120, 44, HIT_SETUP)) ft.drawCircle(36, 104, 20, color_button_active);
  ft.drawCircle(36, 104, 18, setup_focus ? color_button_active : color_button);
  ft.drawText(36, 104, 29, setup_focus ? color_text_active : color_text, FT81x_OPT_CENTER, "S");
  ft.drawText(62, 104, 28, color_text, FT81x_OPT_CENTERY, "Setup");

  app_core_browser_ft_decor(hw_setup, true);

  hover = ft_hover();
  ft_frame_end();
}


core_list_item_t app_core_browser_get_item() {
  core_list_item_t core;
  file1.getName(core.filename, sizeof(core.filename));
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
    if (root1.isOpen()) {
      root1.close();
    }
    if (!root1.open(&sd1, "/")) {
      return;
    }
    root1.rewind();
    while (file1.openNext(&root1, O_RDONLY)) {
      char filename[32]; file1.getName(filename, sizeof(filename));
      if (kg_name_has_ext(filename, CORE_EXT)) {
        cores[cores_len] = app_core_browser_get_item();
        cores_len++;
      }
      file1.close();
    }
  }
  // sort by core order number
  std::sort(cores, cores + cores_len);
  
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
static bool setup_focus_key() {
  uint8_t key = usb_keyboard_report.keycode[0];
  uint16_t joy = joyL | joyR;
  if (key == KEY_ENTER || key == KEY_KPENTER || (joy & (SC_BTN_A | SC_BTN_B))) {
    open_setup();
    return true;
  }
  if (key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN || (joy & (SC_BTN_RIGHT | SC_BTN_UP | SC_BTN_DOWN))) {
    setup_focus = false;
    app_core_browser_ft_menu(1);
    return true;
  }
  return false;
}

void app_core_browser_on_keyboard() {
      if (has_ft) ft_mouse_hide(); // the keys move the selection
      if (!has_ft) setup_focus = false;
      if (setup_focus && setup_focus_key()) return;
      if (cores_len > 0) {
        // down
        if (usb_keyboard_report.keycode[0] == KEY_DOWN || (joyL & SC_BTN_DOWN) || (joyR & SC_BTN_DOWN)) {
          autoload_enabled = false;
          if (core_sel < cores_len-1) {
            core_sel++;
          } else {
            core_sel = 0;
          }
        }

        // up
        if (usb_keyboard_report.keycode[0] == KEY_UP  || (joyL & SC_BTN_UP) || (joyR & SC_BTN_UP)) {
          autoload_enabled = false;
          if (core_sel > 0) {
            core_sel--;
          } else {
            core_sel = cores_len-1;
          }
        }

        // right
        if (usb_keyboard_report.keycode[0] == KEY_RIGHT || (joyL & SC_BTN_RIGHT) || (joyR & SC_BTN_RIGHT)) {
          autoload_enabled = false;
          if (core_sel + page_size() <= cores_len-1) {
            core_sel += page_size();
          } else {
            core_sel = cores_len-1;
          }
        }

        // left
        if (usb_keyboard_report.keycode[0] == KEY_LEFT || (joyL & SC_BTN_LEFT) || (joyR & SC_BTN_LEFT)) {
          autoload_enabled = false;
          if (has_ft && core_sel == 0) {
            // already on the first core: onto the Setup button (not by a held Left)
            if (!ui_repeat) setup_focus = true;
          } else if (core_sel - page_size() >= 0) {
            core_sel -= page_size();
          } else {
            core_sel = 0;
          }
        }
        
        // enter
        if (usb_keyboard_report.keycode[0] == KEY_ENTER || (joyL & SC_BTN_A) || (joyR & SC_BTN_A) || (joyL & SC_BTN_B) || (joyR & SC_BTN_B)) {
          start_core();
        }

        if (usb_keyboard_report.keycode[0] == KEY_V && hw_setup.debug_enabled && has_ft) {
          autoload_enabled = false;
          uint8_t old_vmode = hw_setup.ft_video_mode;
          hw_setup.ft_video_mode = (hw_setup.ft_video_mode + 1) % FT812::modeCount();
          d_printf("Changing video mode from %d to %d", old_vmode, hw_setup.ft_video_mode); d_println();
          if (ft.init(hw_setup.ft_video_mode)) {
            d_printf("FT81x system clock %lu kHz, attempt %u", (unsigned long) (ft.initClockHz() / 1000), ft.initAttempts()); d_println();
          } else {
            d_println("FT81x did not come up in the new video mode");
          }
          app_core_browser_ft_overlay();
        }

        // enter setup mode (S, joy start)
        if (usb_keyboard_report.keycode[0] == KEY_S || (joyL & SC_BTN_START) || (joyR & SC_BTN_START)) {
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
      if (usb_keyboard_report.keycode[0] == KEY_SPACE && has_ft) {
        has_ft = false;
        ft.vga(false);
        ft.spi(false);
        app_core_browser_overlay();
      }

      // the graphic menu, also when it is off in karabas.ini (until the next start)
      if (usb_keyboard_report.keycode[0] == KEY_G && !has_ft) {
        autoload_enabled = false;
        ft.spi(true);
        has_ft = ft.init(hw_setup.ft_video_mode);
        if (has_ft) {
          d_printf("FT81x started (system clock %lu kHz), switching to FT OSD", (unsigned long) (ft.initClockHz() / 1000)); d_println();
          ft.vga(true);
          app_core_browser_ft_overlay();
        } else {
          d_println("FT81x did not come up");
          ft.spi(false);
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
