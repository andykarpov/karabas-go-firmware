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
  ft.setSound(FT81x_SOUND_SWITCH, 40);
  ft.loadImage(0, LOGO_SIZE, logoData, false); // karabas logo, starts from 0 in GRAM
  ft.loadImage(LOGO_BITMAP_SIZE, BURATO2_SIZE, burato2Data, false); // karabas bg, starts from 1792 in GRAM
  ft.writeGRAM(LOGO_BITMAP_SIZE + BURATO3_BITMAP_SIZE, KEY_SIZE, keyData); // pcm sound, starts from 65792 in GRAM
  app_core_browser_ft_menu(0);  
}

void app_core_browser_ft_menu(uint8_t play_sounds) {

  ft.beginDisplayList();

  uint32_t color_black = FT81x_COLOR_RGB(1, 1, 1);
  uint32_t color_gradient = FT81x_COLOR_RGB(200, 0, 0);
  uint32_t color_button = FT81x_COLOR_RGB(32, 32, 38);
  uint32_t color_button_active = FT81x_COLOR_RGB(200, 0, 0);
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
    ft.drawButton(160+8, offset + pos*40, 320-16, 32, 28, color_text, color, FT81x_OPT_3D, name);
    if (cores[i].flash) {
      ft.drawText(160+320-16-24, offset + pos*40 + 16, 28, color_text, FT81x_OPT_CENTERY, "F\0");
    }
    pos++;
  }

  ft.drawText(112, 440, 27, color_copyright, FT81x_OPT_CENTER, "www.karabas.uk\0");
  ft.drawText(112, 460, 27, color_copyright, FT81x_OPT_CENTER, "(c) 2024 andykarpov\0");
  char b[40]; sprintf(b, "Page %d of %d\0", core_page, core_pages);
  ft.drawText(320, 440, 27, color_copyright, FT81x_OPT_CENTER, b);

  char time[9];
  sprintf(time, "%02d:%02d:%02d\0", zxrtc.getHour(), zxrtc.getMinute(), zxrtc.getSecond());
  ft.drawText(ft.width()-88, 30, 30, color_text, FT81x_OPT_CENTER, time);
  char date[9];
  sprintf(date, "%02d/%02d/%02d\0", zxrtc.getDay(), zxrtc.getMonth(), zxrtc.getYear() % 100);
  ft.drawText(ft.width()-88, 60, 30, color_text, FT81x_OPT_CENTER, date);


  if (play_sounds == 1) {
      ft.playSound();
  } else if (play_sounds == 2) {
    ft.playAudio(LOGO_BITMAP_SIZE + BURATO3_BITMAP_SIZE, KEY_SIZE, 22050, FT81x_AUDIO_FORMAT_ULAW, false);
  }

  ft.drawBitmap(0, 8, 8, 56, 16, 4, 0); 
  ft.overlayBitmap(LOGO_BITMAP_SIZE, 640-200, 480-143, 200, 143, 1, 0);
  //ft.overlayBitmap(LOGO_BITMAP_SIZE, 640-160-8, 480-200-8, 160, 200, 1, 0);

  /*uint16_t h = (uint16_t) (zxrtc.getHour() % 12);
  uint16_t m = (uint16_t) (zxrtc.getMinute() % 60);
  uint16_t s = (uint16_t) (zxrtc.getSecond() % 60);
  ft.drawClock(640 - 80-8, 80+8, 80, FT81x_COLOR_RGB(255,255,255), FT81x_COLOR_RGB(1,1,1), FT81x_OPT_NOBACK | FT81x_OPT_FLAT, h, m, s);*/

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
      halt("open root");
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
            delay(500);
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

