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

core_list_item_t cores[MAX_CORES];
uint8_t cores_len = 0;
uint8_t core_sel = 0;
const uint8_t core_page_size = MAX_CORES_PER_PAGE;
const uint8_t ft_core_page_size = MAX_CORES_PER_PAGE/2;
uint8_t core_pages = 1;
uint8_t core_page = 1;
uint16_t rot = 0;

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
  ft.writeGRAM(KEY_OFFSET, KEY_SIZE, keyData); // pcm sound, starts from after logo and bg in GRAM
  app_core_browser_ft_menu(0);  
}

void app_core_browser_ft_menu(uint8_t play_sounds) {

  ft.beginDisplayList();

  uint32_t color_black = FT81x_COLOR_RGB(1, 1, 1);
  uint32_t color_gradient = FT81x_COLOR_RGB(0, 0, 200);
  uint32_t color_button = FT81x_COLOR_RGB(32, 32, 38);
  uint32_t color_button_active = FT81x_COLOR_RGB(0, 0, 200);
  uint32_t color_text = FT81x_COLOR_RGB(255,255,255);
  uint32_t color_copyright = FT81x_COLOR_RGB(120, 120, 120);

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
    const uint32_t color = i == core_sel ? color_button_active : color_button;
    ft.drawButton(ft.width()/4 + 8, offset + pos*40, ft.width()/2-16, 32, 28, color_text, color, FT81x_OPT_3D, name);
    if (cores[i].flash) {
      ft.drawText(ft.width()/4+ft.width()/2-16-24, offset + pos*40 + 16, 28, color_text, FT81x_OPT_CENTERY, "F\0");
    }
    pos++;
  }

  ft.drawText(112, ft.height()-40, 27, color_copyright, FT81x_OPT_CENTER, "www.karabas.uk\0");
  ft.drawText(112, ft.height()-20, 27, color_copyright, FT81x_OPT_CENTER, "(c) 2024 andykarpov\0");
  char b[40]; sprintf(b, "Page %d of %d\0", core_page, core_pages);
  ft.drawText(ft.width()/2, ft.height()-40, 27, color_copyright, FT81x_OPT_CENTER, b);

  char time[9];
  sprintf(time, "%02d:%02d:%02d\0", zxrtc.getHour(), zxrtc.getMinute(), zxrtc.getSecond());
  ft.drawText(ft.width()-88, 30, 30, color_text, FT81x_OPT_CENTER, time);
  char date[9];
  sprintf(date, "%02d/%02d/%02d\0", zxrtc.getDay(), zxrtc.getMonth(), zxrtc.getYear() % 100);
  ft.drawText(ft.width()-88, 60, 30, color_text, FT81x_OPT_CENTER, date);


  if (play_sounds == 1) {
      ft.playSound();
  } else if (play_sounds == 2) {
    ft.playAudio(LOGO_BITMAP_SIZE + BG_BITMAP_SIZE, KEY_SIZE, KEY_SAMPLERATE, FT81x_AUDIO_FORMAT_ULAW, false);
    delay(KEY_DURATION);
    ft.stopSound();
  }

  ft.drawBitmap(0, 8, 8, LOGO_WIDTH, LOGO_HEIGHT, 4, 0);  // logo scaled 4x
  ft.overlayBitmap(LOGO_BITMAP_SIZE, ft.width()-BG_WIDTH-8, ft.height()-BG_HEIGHT-8, BG_WIDTH, BG_HEIGHT, 1, 0); // bg image

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
    sd1.chvol();
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
  // TODO
}

void app_core_browser_on_keyboard() {
          if (cores_len > 0) {
        // down
        if (usb_keyboard_report.keycode[0] == KEY_DOWN || (joyL & SC_BTN_DOWN) || (joyR & SC_BTN_DOWN)) {
          if (core_sel < cores_len-1) {
            core_sel++;
          } else {
            core_sel = 0;
          }
        }

        // up
        if (usb_keyboard_report.keycode[0] == KEY_UP  || (joyL & SC_BTN_UP) || (joyR & SC_BTN_UP)) {
          if (core_sel > 0) {
            core_sel--;
          } else {
            core_sel = cores_len-1;
          }
        }

        // right
        if (usb_keyboard_report.keycode[0] == KEY_RIGHT || (joyL & SC_BTN_RIGHT) || (joyR & SC_BTN_RIGHT)) {
          if (core_sel + core_page_size <= cores_len-1) {
            core_sel += core_page_size;
          } else {
            core_sel = cores_len-1;
          }
        }

        // left
        if (usb_keyboard_report.keycode[0] == KEY_LEFT || (joyL & SC_BTN_LEFT) || (joyR & SC_BTN_LEFT)) {
          if (core_sel - core_page_size >= 0) {
            core_sel -= core_page_size;
          } else {
            core_sel = 0;
          }
        }
        
        // enter
        if (usb_keyboard_report.keycode[0] == KEY_ENTER || (joyL & SC_BTN_A) || (joyR & SC_BTN_A) || (joyL & SC_BTN_B) || (joyR & SC_BTN_B)) {
          if (FT_OSD == 1 && has_ft == true) {
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

        // redraw core browser on keypress
        if (osd_state == state_core_browser) {
          if (FT_OSD == 1 && has_ft == true) {
            app_core_browser_ft_menu(1);
          } else {
            app_core_browser_menu(APP_COREBROWSER_MENU_OFFSET);
          }
        }
      }

      // return back to classic osd
      if (usb_keyboard_report.keycode[0] == KEY_SPACE && has_ft == true && FT_OSD == 1) {
        has_ft = false;
        ft.vga(false);
        ft.spi(false);
        app_core_browser_overlay();
      }
}

bool operator<(const core_list_item_t a, const core_list_item_t b) {
  return a.order < b.order;
}

