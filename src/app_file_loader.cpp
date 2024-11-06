#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "OSD.h"
#include "usb_hid_keys.h"
#include "SegaController.h"
#include "SdFat.h"
#include <algorithm>
#include <tuple>
#include "app_file_loader.h"
#include <SPI.h>

#define SD2_CONFIG SdSpiConfig(PIN_MCU_SD2_CS, SHARED_SPI, SD_SCK_MHZ(16)) // SD2 SPI Settings

file_list_item_t files[MAX_FILES];
uint8_t files_len = 0;
uint8_t file_sel = 0;
const uint8_t file_page_size = MAX_CORES_PER_PAGE;
uint8_t file_pages = 1;
uint8_t file_page = 1;

void app_file_loader_read_list(bool forceIndex = false) {

  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.frame(8,8,24,12, 1);
  zxosd.fill(9,9,23,11, 32);
  zxosd.setColor(OSD::COLOR_GREEN_I, OSD::COLOR_BLACK);
  zxosd.setPos(9, 9);
  zxosd.print("Scanning files");
  zxosd.setColor(OSD::COLOR_MAGENTA_I, OSD::COLOR_BLACK);
  zxosd.setPos(9,10);
  zxosd.print("Please wait...");

  // files from sd2 card

  ft.sd2(true);

  if (!sd2.begin(SD2_CONFIG)) {
    d_println("SD2 failed to begin");
    has_sd2 = false;
  } else {
    has_sd2 = true;
  }

  if (has_sd2) {
    String dir = String(core.dir);
    dir.trim();
    if (dir.length() == 0) dir = "/";
    if (dir.charAt(0) != '/') dir = "/" + dir;
    char d[256];
    dir.toCharArray(d, 255);
    if (root2.isOpen()) {
      root2.close();
    }
    if (!root2.open(d)) {

        zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
        zxosd.frame(8,8,24,12, 1);
        zxosd.fill(9,9,23,11, 32);
        zxosd.setColor(OSD::COLOR_RED_I, OSD::COLOR_BLACK);
        zxosd.setPos(9, 9);
        zxosd.print("Error occured");
        zxosd.setPos(9,10);
        zxosd.printf("Bad dir %s", d);
        return;
    }
    root2.rewind();

    // recreate index
    if (forceIndex || !sd2.exists(dir + "/index.db")) {
      // drop index
      if (sd2.exists(dir + "/index.db")) {
        sd2.remove(dir + "/index.db");
      }
      if (fileIndex2 = sd2.open(dir + "/index.db", FILE_WRITE)) {
        d_println("Read file list");
        String exts = String(core.file_extensions);
        exts.toLowerCase(); exts.trim();
        char e[33];
        exts.toCharArray(e, 32);
        int cnt = 0;
        while (file2.openNext(&root2, O_RDONLY)) {
          char filename[255]; file2.getName(filename, sizeof(filename));
          uint8_t len = strlen(filename);
          if (cnt < MAX_FILES && !file2.isDirectory() && len <= 32) {
            if (exts.length() == 0 || exts.indexOf(strlwr(filename + (len - 4))) != -1) {
              fileIndex2.write(filename);
              fileIndex2.write("\n");
              cnt++;
            }
          }
          file2.close();
        }
        fileIndex2.close();
      }
    }

    // reading file list from index
    if (fileIndex2 = sd2.open(dir + "/index.db", FILE_READ)) {
      files_len = 0;
      while(fileIndex2.available()) {
        String s = fileIndex2.readStringUntil('\n'); 
        if (s.length() > 0 && s.length() <= 32) {
          s.trim();
          file_list_item_t f;
          s.toCharArray(f.name, sizeof(f.name));
          files[files_len] = f;
          files_len++;
        }
      }
      fileIndex2.close();
    }
    
  } else {
    zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
    zxosd.frame(8,8,24,12, 1);
    zxosd.fill(9,9,23,11, 32);
    zxosd.setColor(OSD::COLOR_RED_I, OSD::COLOR_BLACK);
    zxosd.setPos(9, 9);
    zxosd.print("Error occured");
    zxosd.setPos(9,10);
    zxosd.print("Check SD card");
    return;
  }
  // sort by file name
  std::sort(files, files + files_len);
  
  // cleanup error messages
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.fill(8,8,24,12, 32);  

  // select and load prev file on boot
  if (files_len >0 && strlen(core.last_file) > 0) {
    String lf = String(core.last_file); lf.trim();
    for (uint8_t i=0; i<files_len; i++) {
      String f = String(files[i].name); f.trim();
      if (f == lf) {
          file_sel = i;
          app_file_loader_send_file(files[file_sel].name);
          // hide osd
          if (!is_osd_hiding) {
            is_osd_hiding = true;
            hide_timer.reset();
            zxosd.hideMenu();
          }
      }
    }
  }
}

void app_file_loader_menu(uint8_t vpos) {
  file_pages = ceil((float)files_len / file_page_size);
  file_page = ceil((float)(file_sel+1)/file_page_size);
  uint8_t file_from = (file_page-1)*file_page_size;
  uint8_t file_to = file_page*file_page_size > files_len ? files_len : file_page*file_page_size;
  uint8_t file_fill = file_page*file_page_size;
  uint8_t pos = vpos;
  for(uint8_t i=file_from; i < file_to; i++) {
    zxosd.setPos(0, pos);
    if (file_sel == i) {
      zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLUE);
    } else {
      zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
    }
    // display 28 chars
    char name[29]; memcpy(name, files[i].name, 28); name[28] = '\0';
    zxosd.printf("%-3d ", i+1); 
    zxosd.print(name);
    // fill name with spaces
    uint8_t l = strlen(name);
    if (l < 28) {
      for (uint8_t ll=l; ll<28; ll++) {
        zxosd.print(" ");
      }
    }
    pos++;
  }
  if (file_fill > file_to) {
    for (uint8_t i=file_to; i<file_fill; i++) {
      zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
      for (uint8_t j=0; j<32; j++) {
        zxosd.print(" ");
      }
    }
  }
  zxosd.setPos(8, vpos + file_page_size + 1); zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.printf("Page %02d of %02d", file_page, file_pages);
}

void app_file_loader_overlay(bool initSD = true, bool recreateIndex = false) {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();

  zxosd.header(core.build, core.id, HW_ID);

  zxosd.setPos(0,5);

  // collect files from sd
  if (initSD) {
      app_file_loader_read_list(recreateIndex);
  }

  app_file_loader_menu(APP_COREBROWSER_MENU_OFFSET);  

  // footer
  zxosd.line(21);
  zxosd.line(23);
  zxosd.setPos(1,24); zxosd.print("Please use arrows to navigate");
  zxosd.setPos(1,25); zxosd.print("Press Enter to load selection");
}

void app_file_loader_save()
{
    sd1.chvol();
    if (!file1.open(core.filename, FILE_WRITE)) {
      d_println("Unable to open bitstream file to write");
      return;
    }
    if (!file1.isWritable()) {
      d_println("File is not writable");
      return;
    }
    memcpy(core.last_file, files[file_sel].name, 32);
    file1.seek(FILE_POS_FILELOADER_FILE);
    file1.write(core.last_file, 32);
    file1.close();
}

void app_file_loader_send_file(const char* filename) {
  
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.frame(8,8,24,12, 1);
  zxosd.fill(9,9,23,11, 32);
  zxosd.setColor(OSD::COLOR_GREEN_I, OSD::COLOR_BLACK);
  zxosd.setPos(9, 9);
  zxosd.print("Preparing...   ");
  zxosd.setColor(OSD::COLOR_MAGENTA_I, OSD::COLOR_BLACK);
  zxosd.setPos(9,10);
  zxosd.print("Please wait.   ");

  String dir = String(core.dir);
  dir.trim();
  if (dir.length() == 0) dir = "/";
  if (dir.charAt(0) != '/') dir = "/" + dir;
  String f = String(filename);
  f.trim();
  String fullpath = dir + "/" + f;
  fullpath.trim();
  char new_filename[255+1];
  fullpath.toCharArray(new_filename, 255, 0);

  if (!file2.open(&sd2, new_filename, FILE_READ)) {
    d_printf("Unable to open file %s", new_filename); d_println();
    d_flush();
    app_file_loader_overlay(false, false);
    return;
  }

  uint8_t buf[512];
  int c;

  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.frame(8,8,24,12, 1);
  zxosd.fill(9,9,23,11, 32);
  zxosd.setColor(OSD::COLOR_YELLOW_I, OSD::COLOR_BLACK);
  zxosd.setPos(9, 9);
  zxosd.print("Loading...     ");
  zxosd.setColor(OSD::COLOR_CYAN_I, OSD::COLOR_BLACK);
  zxosd.setPos(9,10);
  zxosd.print("Please wait.   ");
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);

  // trigger reset
  spi_send(CMD_FILELOADER, 0, 1);
  spi_send(CMD_FILELOADER, 0, 0);

  uint64_t file_size = file2.size();

  d_printf("Sending file %s (%d bytes)", new_filename, (uint32_t)((file_size))); d_println();
  d_flush();
  
uint32_t cnt = 0;
float perc = 0;

  while(c = file2.read(buf, sizeof(buf))) {
    for (int j=0; j<c; j++) {
      app_file_loader_send_byte(cnt, buf[j]);
      cnt++;
      if (cnt % 8192 == 0) {
        //perc = (file_size > 0) ? (((float) cnt) * 100.0) / ((float) file_size) : 0.0;
        //zxosd.setPos(9,11);
        //zxosd.printf("%d bytes ", cnt);
        zxosd.progress(9, 11, 15, cnt, file_size);
        //zxosd.printf("%-3d ", (perc >=0 && perc <= 100) ? (uint8_t) perc : 100); zxosd.print("% ");
      }
    }
  }
  zxosd.setColor(OSD::COLOR_GREEN_I, OSD::COLOR_BLACK);
  //zxosd.setPos(9,11);
  zxosd.progress(9, 11, 15, file_size, file_size);
  //zxosd.print("100 % loaded :)");
  
  file2.close();
  d_printf("Sent %u bytes", cnt); d_println();
  delay(500);

  // reload menu
  app_file_loader_overlay(false, false);
}

void app_file_loader_on_keyboard() 
{
          if (files_len > 0) {
        // down
        if (usb_keyboard_report.keycode[0] == KEY_DOWN || (joyL & SC_BTN_DOWN) || (joyR & SC_BTN_DOWN)) {
          if (file_sel < files_len-1) {
            file_sel++;
          } else {
            file_sel = 0;
          }
        }

        // up
        if (usb_keyboard_report.keycode[0] == KEY_UP || (joyL & SC_BTN_UP) || (joyR & SC_BTN_UP)) {
          if (file_sel > 0) {
            file_sel--;
          } else {
            file_sel = files_len-1;
          }
        }

        // right
        if (usb_keyboard_report.keycode[0] == KEY_RIGHT || (joyL & SC_BTN_RIGHT) || (joyR & SC_BTN_RIGHT)) {
          if (file_sel + file_page_size <= files_len-1) {
            file_sel += file_page_size;
          } else {
            file_sel = files_len-1;
          }
        }

        // left
        if (usb_keyboard_report.keycode[0] == KEY_LEFT || (joyL & SC_BTN_LEFT) || (joyR & SC_BTN_LEFT)) {
          if (file_sel - file_page_size >= 0) {
            file_sel -= file_page_size;
          } else {
            file_sel = 0;
          }
        }

        // R = recreate file index
        if (usb_keyboard_report.keycode[0] == KEY_R) {
            file_sel = 0;
            app_file_loader_overlay(true, true);
        }
        
        // enter
        if (usb_keyboard_report.keycode[0] == KEY_ENTER || (joyL & SC_BTN_A) || (joyR & SC_BTN_A) || (joyL & SC_BTN_B) || (joyR & SC_BTN_B)) {
          app_file_loader_save();
          app_file_loader_send_file(files[file_sel].name);
          // hide osd
          if (!is_osd_hiding) {
            is_osd_hiding = true;
            hide_timer.reset();
            zxosd.hideMenu();
          }
        }

        // redraw file loader on keypress
        if (osd_state == state_file_loader) {
          app_file_loader_menu(APP_COREBROWSER_MENU_OFFSET);
        }
      }
}

void app_file_loader_send_byte(uint32_t addr, uint8_t data) {
  // send file bank address every 256 bytes
  if (addr % 256 == 0) {
    uint8_t filebank3 = (uint8_t)((addr & 0xFF000000) >> 24);
    uint8_t filebank2 = (uint8_t)((addr & 0x00FF0000) >> 16);
    uint8_t filebank1 = (uint8_t)((addr & 0x0000FF00) >> 8);
    //d_printf("File bank %d %d %d", filebank1, filebank2, filebank3); d_println();
    spi_send(CMD_FILEBANK, 0, filebank1);
    spi_send(CMD_FILEBANK, 1, filebank2);
    spi_send(CMD_FILEBANK, 2, filebank3);
  }
  // send lower 256 bytes
  uint8_t fileaddr = (uint8_t)((addr & 0x000000FF));
  spi_send(CMD_FILEDATA, fileaddr, data);
}

bool operator<(const file_list_item_t a, const file_list_item_t b) {
  // Lexicographically compare the tuples (hour, minute)
  String s1 = String(a.name); String s2 = String(b.name);
  s1.toLowerCase(); s2.toLowerCase();
  return s1 < s2;
}
