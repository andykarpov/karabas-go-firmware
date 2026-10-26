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
ElapsedTimer autoload_timer;
bool autoload_enabled;
uint32_t autoload_countdown;

// the clickable areas of the graphic browser besides the cores (0..cores_len-1)
enum { HIT_SETUP = 1000, HIT_PREV, HIT_NEXT };
static int16_t hover = FT_HIT_NONE; // under the pointer at the last draw

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

static_assert(KEY1_PCM_START + KEY1_PCM_SIZE <= sizeof(key1Data), "sound 1 is past its array");
static_assert(KEY2_PCM_START + KEY2_PCM_SIZE <= sizeof(key2Data), "sound 2 is past its array");
static_assert(KEY3_PCM_START + KEY3_PCM_SIZE <= sizeof(key3Data), "sound 3 is past its array");
static_assert(KEY4_PCM_START + KEY4_PCM_SIZE <= sizeof(key4Data), "sound 4 is past its array");

void app_core_browser_ft_load() {
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
  ft.drawGradient(ft.width()/2, 0, t.color_gradient, ft.width()/2, ft.height()-ft.height()/4-1, t.color_bg);
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
    char name[18];
    String n = String(cores[i].name); n.trim(); n.toCharArray(name, 18);
    const uint32_t colorb = i == core_sel ? color_button_active : color_button;
    const uint32_t colort = i == core_sel ? color_text_active : color_text;
    if (ft_hit_add(ft.width()/4 + 8, offset + pos*FT_CORE_STEP, ft.width()/2-16, 32, i)) {
      // hovered: a rect grows by its corner radius, so this is a 2 px frame around the button
      ft.drawRect(ft.width()/4 + 8, offset + pos*FT_CORE_STEP, ft.width()/2-16, 32, 2, color_button_active);
    }
    ft.drawButton(ft.width()/4 + 8, offset + pos*FT_CORE_STEP, ft.width()/2-16, 32, 28, colort, colorb, (hw_setup.ft_3d_buttons) ? FT81x_OPT_3D : FT81x_OPT_FLAT , name);
    if (cores[i].flash) {
      ft.drawText(ft.width()/4+ft.width()/2-16-24, offset + pos*FT_CORE_STEP + 16, 28, colort, FT81x_OPT_CENTERY, "F\0");
    }
    if (autoload_enabled && i==core_sel) {
      uint32_t diff = (autoload_timer.elapsed() < autoload_countdown * 1000) ? ceil((autoload_countdown * 1000 - autoload_timer.elapsed())/1000) : 0;
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
    uint32_t diff = (autoload_timer.elapsed() < autoload_countdown * 1000) ? ceil((autoload_countdown * 1000 - autoload_timer.elapsed())/1000) : 0;
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
  ft.drawCircle(36, 104, 18, color_button);
  ft.drawText(36, 104, 29, color_text, FT81x_OPT_CENTER, "S");
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
    String s1 = String(hw_setup.autoload_core);
    String s2 = String(cores[i].id);
    s1.trim();
    s2.trim();
//    d_println(s1);
//    d_println(s2);
//    d_println(hw_setup.autoload_enabled);
    if (!autoload_enabled && hw_setup.autoload_enabled && s2.equals(s1)) {
      autoload_enabled = true;
      autoload_countdown = hw_setup.autoload_timeout;
      core_sel = i;
      autoload_timer.reset();
    }
  }

}

static void start_core() {
  autoload_enabled = false;
  if (has_ft) {
    app_core_browser_ft_menu(2); // play wav
  }
  d_printf("Selected core %s to boot from menu", cores[core_sel].filename); d_println();
  String f = String(cores[core_sel].filename); f.trim(); 
  char buf[32]; f.toCharArray(buf, sizeof(buf));
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
  osd_return_state = state_core_browser;
  osd_state = has_ft ? state_ft_setup : state_setup;
}

void app_core_browser_on_keyboard() {
      if (has_ft) ft_mouse_hide(); // the keys move the selection
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
          if (core_sel - page_size() >= 0) {
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
          hw_setup.ft_video_mode++;
          if (hw_setup.ft_video_mode > 14) {
            hw_setup.ft_video_mode = 0;
          }
          d_printf("Changing video mode from %d to %d", old_vmode, hw_setup.ft_video_mode); d_println();
          if (ft.init(hw_setup.ft_video_mode)) {
            d_printf("FT81x system clock %lu kHz, attempt %u", (unsigned long) (ft.initClockHz() / 1000), ft.initAttempts()); d_println();
          } else {
            d_println("FT81x did not come up in the new video mode");
          }
          app_core_browser_ft_overlay();
        }

        // enter setup mode
        if (usb_keyboard_report.keycode[0] == KEY_S) {
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
    if (autoload_timer.elapsed() > autoload_countdown * 1000) {
      autoload_enabled = false;
      String f = String(cores[core_sel].filename); f.trim(); 
      char buf[32]; f.toCharArray(buf, sizeof(buf));
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
