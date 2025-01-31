#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "FT812.h"
#include "bitmaps.h"
#include "OSD.h"
#include "SdFat.h"
#include "LittleFS.h"
#include "SegaController.h"
#include "usb_hid_keys.h"
#include <algorithm>
#include <tuple>
#include "app_core_browser.h"
#include "sorts.h"

core_list_item_t cores[MAX_CORES];
uint8_t cores_len = 0;
uint8_t core_sel = 0;
const uint8_t core_page_size = MAX_CORES_PER_PAGE;
const uint8_t ft_core_page_size = MAX_CORES_PER_PAGE/2;
uint8_t core_pages = 1;
uint8_t core_page = 1;
uint16_t rot = 0;
ElapsedTimer autoload_timer;
bool autoload_enabled;
uint32_t autoload_countdown;

void app_core_browser_overlay() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();
  zxosd.header(core.build, core.id, HW_ID);
  zxosd.setPos(0,5);
  app_core_browser_menu(APP_COREBROWSER_MENU_OFFSET);  
  // footer
  zxosd.line(21);
  zxosd.line(23);
  zxosd.setPos(1,24); zxosd.print("Please use arrows to navigate");
  zxosd.setPos(1,25); zxosd.print("Press Enter to load selection");
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
      zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLUE);
    } else {
      zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
    }
    char name[18]; memcpy(name, cores[i].name, 17); name[17] = '\0';
    zxosd.printf("%-3d ", i+1); 
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
  zxosd.printf("Page %02d of %02d", core_page, core_pages);
}

void app_core_browser_ft_overlay() {
  ft.setSound(FT81x_SOUND_COWBELL, 60);
  ft.loadImage(LOGO_OFFSET, LOGO_SIZE, logoData, false); // karabas logo, starts from 0 in GRAM
  ft.loadImage(BG_OFFSET, BG_SIZE, bgData, false); // karabas bg, starts after logo in GRAM
  ft.writeGRAM(KEY1_OFFSET, KEY1_SIZE, key1Data); // pcm sound, starts from after logo and bg in GRAM
  ft.writeGRAM(KEY2_OFFSET, KEY2_SIZE, key2Data); // pcm sound, starts from after logo and bg in GRAM
  ft.writeGRAM(KEY3_OFFSET, KEY3_SIZE, key3Data); // pcm sound, starts from after logo and bg in GRAM
  ft.writeGRAM(KEY4_OFFSET, KEY4_SIZE, key4Data); // pcm sound, starts from after logo and bg in GRAM
  app_core_browser_ft_menu(0);
}

void app_core_browser_ft_menu(uint8_t play_sounds) {

  ft.beginDisplayList();

  uint32_t color_black = hw_setup.color_bg;
  uint32_t color_gradient = hw_setup.color_gradient;
  uint32_t color_button = hw_setup.color_button;
  uint32_t color_button_active = hw_setup.color_active;
  uint32_t color_text = hw_setup.color_text;
  uint32_t color_text_active = hw_setup.color_text_active;
  uint32_t color_copyright = hw_setup.color_copyright;

  ft.clear(color_black);
  ft.drawGradient(ft.width()/2, 0, color_gradient, ft.width()/2, ft.height()-ft.height()/4-1, color_black);

  core_pages = ceil((float)cores_len / ft_core_page_size);
  core_page = ceil((float)(core_sel+1)/ft_core_page_size);
  uint8_t core_from = (core_page-1)*ft_core_page_size;
  uint8_t core_to = core_page*ft_core_page_size > cores_len ? cores_len : core_page*ft_core_page_size;
  uint8_t core_fill = core_page*ft_core_page_size;
  uint8_t pos = 0;
  uint8_t offset = 64 + 16 + 8;
  for(uint8_t i=core_from; i < core_to; i++) {
    char name[18];
    String n = String(cores[i].name); n.trim(); n.toCharArray(name, 18);
    const uint32_t colorb = i == core_sel ? color_button_active : color_button;
    const uint32_t colort = i == core_sel ? color_text_active : color_text;
    ft.drawButton(ft.width()/4 + 8, offset + pos*40, ft.width()/2-16, 32, 28, colort, colorb, (hw_setup.ft_3d_buttons) ? FT81x_OPT_3D : FT81x_OPT_FLAT , name);
    if (cores[i].flash) {
      ft.drawText(ft.width()/4+ft.width()/2-16-24, offset + pos*40 + 16, 28, colort, FT81x_OPT_CENTERY, "F\0");
    }
    if (autoload_enabled && i==core_sel) {
      uint32_t diff = (autoload_timer.elapsed() < autoload_countdown * 1000) ? ceil((autoload_countdown * 1000 - autoload_timer.elapsed())/1000) : 0;
      ft.drawGauge(ft.width()/4+16+12, offset + pos*40 + 16, 12, colort, colorb, 0, 1, 1, diff, autoload_countdown);
    }
    pos++;
  }

  char b[40]; 

  //ft.drawText(112, ft.height()-40, 27, color_copyright, FT81x_OPT_CENTER, "www.karabas.uk\0");
  sprintf(b, "FW build: %s\0", STRINGIZE_VALUE_OF(BUILD_VER));
  ft.drawText(112, ft.height()-40, 27, color_copyright, FT81x_OPT_CENTER, b);
  ft.drawText(112, ft.height()-20, 27, color_copyright, FT81x_OPT_CENTER, "(c) 2025 andykarpov\0");
  sprintf(b, "Page %d of %d\0", core_page, core_pages);
  ft.drawText(ft.width()/2, ft.height()-40, 27, color_copyright, FT81x_OPT_CENTER, b);

  if (autoload_enabled) {
    uint32_t diff = (autoload_timer.elapsed() < autoload_countdown * 1000) ? ceil((autoload_countdown * 1000 - autoload_timer.elapsed())/1000) : 0;
    sprintf(b, "Autoloading core in %d s\0", diff);
    ft.drawText(ft.width()/2, ft.height()-20, 27, color_copyright, FT81x_OPT_CENTER, b);
  } else {
    sprintf(b, "Autoload disabled\0");
    ft.drawText(ft.width()/2, ft.height()-20, 27, color_copyright, FT81x_OPT_CENTER, b);
  }

  if (hw_setup.ft_time) {
    char time[9];
    sprintf(time, "%02d:%02d:%02d\0", zxrtc.getHour(), zxrtc.getMinute(), zxrtc.getSecond());
    ft.drawText(ft.width()-88, 30, 30, color_text, FT81x_OPT_CENTER, time);
  }
  if (hw_setup.ft_date) {
    char date[9];
    sprintf(date, "%02d/%02d/%02d\0", zxrtc.getDay(), zxrtc.getMonth(), zxrtc.getYear() % 100);
    ft.drawText(ft.width()-88, 60, 30, color_text, FT81x_OPT_CENTER, date);
  }

  if (play_sounds == 1 && hw_setup.ft_click) {
      ft.playSound();
  } else if (play_sounds == 2 && hw_setup.ft_sound > 0) {
    uint32_t key_size, key_samplerate, key_duration, key_offset;
    switch (hw_setup.ft_sound) {
      case 1:
        key_size = KEY1_SIZE;
        key_samplerate = KEY1_SAMPLERATE;
        key_duration = KEY1_DURATION;
        key_offset = KEY1_OFFSET;
        break;
      case 2:
        key_size = KEY2_SIZE;
        key_samplerate = KEY2_SAMPLERATE;
        key_duration = KEY2_DURATION;
        key_offset = KEY2_OFFSET;
        break;
      case 3:
        key_size = KEY3_SIZE;
        key_samplerate = KEY3_SAMPLERATE;
        key_duration = KEY3_DURATION;
        key_offset = KEY3_OFFSET;
        break;
      case 4:
        key_size = KEY4_SIZE;
        key_samplerate = KEY4_SAMPLERATE;
        key_duration = KEY4_DURATION;
        key_offset = KEY4_OFFSET;
        break;
      default:
        key_size = KEY1_SIZE;
        key_samplerate = KEY1_SAMPLERATE;
        key_duration = KEY1_DURATION;
    }
    ft.playAudio(key_offset, key_size, key_samplerate, FT81x_AUDIO_FORMAT_ULAW, false);
    delay(key_duration);
    ft.stopSound();
  }

  ft.drawBitmap(0, 8, 8, LOGO_WIDTH, LOGO_HEIGHT, 4, 0);  // logo scaled 4x
  if (hw_setup.ft_char) {
    ft.overlayBitmap(LOGO_BITMAP_SIZE, ft.width()-BG_WIDTH-8, ft.height()-BG_HEIGHT-8, BG_WIDTH, BG_HEIGHT, 1, 0); // bg image
  }

  ft.swapScreen();
}


core_list_item_t app_core_browser_get_item(bool is_flash) {
  core_list_item_t core;
  if (is_flash) {
    String s = String(ffile.fullName());
    s = "/" + s;
    s.toCharArray(core.filename, sizeof(core.filename));
    core.flash = true;
  } else {
    file1.getName(core.filename, sizeof(core.filename));
    core.flash = false;
  }
  file_seek(FILE_POS_CORE_ID, is_flash); file_read_bytes(core.id, 32, is_flash); core.id[32] = '\0';
  file_seek(FILE_POS_CORE_NAME, is_flash); file_read_bytes(core.name, 32, is_flash); core.name[32] = '\0';
  uint8_t visible; file_seek(FILE_POS_CORE_VISIBLE, is_flash); visible = file_read(is_flash); core.visible = (visible > 0);
  file_seek(FILE_POS_CORE_ORDER, is_flash); core.order = file_read(is_flash);
  file_seek(FILE_POS_CORE_TYPE, is_flash); core.type = file_read(is_flash);
  file_seek(FILE_POS_CORE_BUILD, is_flash); file_read_bytes(core.build, 8, is_flash); core.build[8] = '\0';
  return core;
}

void app_core_browser_read_list() {

  // files from flash
  if (has_fs) {
    fs::Dir dir = LittleFS.openDir("/");
    dir.rewind();
    while (dir.next()) {
      if (dir.isFile()) {
        ffile = dir.openFile("r");
        char filename[32]; file_get_name(filename, sizeof(filename), true);
        uint8_t len = strlen(filename);
        if (strstr(strlwr(filename + (len - 4)), CORE_EXT)) {
          cores[cores_len] = app_core_browser_get_item(true);
          cores_len++;
        }
        ffile.close();
      }
    }
  }

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
      uint8_t len = strlen(filename);
      if (strstr(strlwr(filename + (len - 4)), CORE_EXT)) {
        cores[cores_len] = app_core_browser_get_item(false);
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

void app_core_browser_on_keyboard() {
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
          if (core_sel + core_page_size <= cores_len-1) {
            core_sel += core_page_size;
          } else {
            core_sel = cores_len-1;
          }
        }

        // left
        if (usb_keyboard_report.keycode[0] == KEY_LEFT || (joyL & SC_BTN_LEFT) || (joyR & SC_BTN_LEFT)) {
          autoload_enabled = false;
          if (core_sel - core_page_size >= 0) {
            core_sel -= core_page_size;
          } else {
            core_sel = 0;
          }
        }
        
        // enter
        if (usb_keyboard_report.keycode[0] == KEY_ENTER || (joyL & SC_BTN_A) || (joyR & SC_BTN_A) || (joyL & SC_BTN_B) || (joyR & SC_BTN_B)) {
          autoload_enabled = false;
          if (hw_setup.ft_enabled && has_ft == true) {
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

        if (usb_keyboard_report.keycode[0] == KEY_V && hw_setup.debug_enabled && hw_setup.ft_enabled && has_ft) {
          autoload_enabled = false;
          uint8_t old_vmode = hw_setup.ft_video_mode;
          hw_setup.ft_video_mode++;
          if (hw_setup.ft_video_mode > 14) {
            hw_setup.ft_video_mode = 0;
          }
          d_printf("Changing video mode from %d to %d", old_vmode, hw_setup.ft_video_mode); d_println();
          ft.init(hw_setup.ft_video_mode);
          app_core_browser_ft_overlay();
        }

        // redraw core browser on keypress
        if (osd_state == state_core_browser) {
          if (hw_setup.ft_enabled && has_ft == true) {
            app_core_browser_ft_menu(1);
          } else {
            app_core_browser_menu(APP_COREBROWSER_MENU_OFFSET);
          }
        }
      }

      // return back to classic osd
      if (usb_keyboard_report.keycode[0] == KEY_SPACE && has_ft == true && hw_setup.ft_enabled) {
        has_ft = false;
        ft.vga(false);
        ft.spi(false);
        app_core_browser_overlay();
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
