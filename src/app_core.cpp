#include <Arduino.h>
#include "types.h"
#include "config.h"
#include "main.h"
#include "OSD.h"
#include "usb_hid_keys.h"
#include "SegaController.h"
#include "SdFat.h"
#include "LittleFS.h"
#include "app_core.h"

uint8_t curr_osd_item;
bool is_filebrowser = false;
bool prev_is_filebrowser = false;

uint8_t filebrowser_len = 0;
uint8_t filebrowser_sel = 0;
uint8_t filebrowser_offset = 0;
uint8_t filebrowser_special = 0;
uint8_t filebrowser_total = 0;
core_filebrowser_item_t filebrowser_files[16];

uint8_t find_first_item() {
  if (core.osd_len > 0) {
    for (uint8_t i = 0; i < core.osd_len; i++) {
      if (core.osd[i].options_len > 0 || core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
        return i;
      }
    }
  }
  return 0;
}

uint8_t find_last_item() {
  if (core.osd_len > 0) {
    for (uint8_t i = core.osd_len-1; i >= 0; i--) {
      if (core.osd[i].options_len > 0 || core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
        return i;
      }
    }
  }
  return 0;
}

uint8_t find_prev_item() {
  uint8_t first = find_first_item();
  uint8_t last = find_last_item();

  // border case with 0 or 1 items
  if (first == last) {
    return first;
  }

  // if there is a room to decrease - find a previous item
  if (curr_osd_item > first) {
    for (uint8_t i = curr_osd_item-1; i >= first; i--) {
      if (core.osd[i].options_len > 0 || core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
        return i;
      }
    }
  }
  // fallback - return last item with options
  return last;
}

uint8_t find_next_item() {
  uint8_t first = find_first_item();
  uint8_t last = find_last_item();

  // border case with 0 or 1 items
  if (first == last) {
    return last;
  }

  // if there is a room to increase - find a next item
  if (curr_osd_item < last) {
    for (uint8_t i = curr_osd_item+1; i <= last; i++) {
      if (core.osd[i].options_len > 0 || core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
        return i;
      }
    }
  }
  // fallback - return first item with options
  return first;

}

void app_core_overlay()
{
  osd_state = state_main;

  // disable popup in overlay mode
  zxosd.hidePopup();

  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();

  zxosd.header(core.build, core.id, HW_ID);

  curr_osd_item = find_first_item();
  app_core_menu(APP_COREBROWSER_MENU_OFFSET);

  // footer
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.line(24);
  zxosd.setPos(0,25); zxosd.print("Press Menu+Esc to toggle OSD");
}

void app_core_menu(uint8_t vpos) {
  for (uint8_t i=0; i<core.osd_len; i++) {
    String name = String(core.osd[i].name);
    String hotkey = String(core.osd[i].hotkey);
    zxosd.setPos(0, i+vpos);
    zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
    // text line (32 chars)
    if (core.osd[i].type == CORE_OSD_TYPE_TEXT) {
      name = name + hotkey;
      zxosd.print(name.substring(0,32));
    // normal osd line (name, option, hotkey)
    } else {
      zxosd.print(name.substring(0,10));

      String option;
      if (core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
        if (file_slots[core.osd[i].slot_id].is_mounted) {
          option = String(file_slots[core.osd[i].slot_id].filename);
        } else {
          option = String("Choose... ");
        }
      } else {
        option = String(core.osd[i].options[core.osd[i].val].name);
      }
      zxosd.setPos(11, i+vpos);
      if (curr_osd_item == i) {
        zxosd.setColor(OSD::COLOR_BLACK, OSD::COLOR_YELLOW_I);
      } else {
        zxosd.setColor(OSD::COLOR_YELLOW_I, OSD::COLOR_BLACK);
      }
      zxosd.print(option.substring(0, 10));

      String hotkey = String(core.osd[i].hotkey);
      zxosd.setColor(OSD::COLOR_CYAN_I, OSD::COLOR_BLACK);
      if (core.osd[i].options_len > 0 || core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
        zxosd.setPos(22, i+vpos);
        zxosd.print(hotkey.substring(0,10));
      } else {
        zxosd.setPos(11, i+vpos);
        zxosd.print(hotkey);
      }
    }
  }
}

void app_core_save(uint8_t pos)
{
  bool is_flash = is_flashfs(core.filename);

  if (is_flash) {
    ffile = LittleFS.open(core.filename, "r+"); // read+write
    core.osd_need_save = false;
    if (core.osd[pos].type == CORE_OSD_TYPE_FILEMOUNTER) {
      // todo: save file mounter data (dir, filename)
    } else if (core.osd[pos].type == CORE_OSD_TYPE_FILELOADER) {
      // todo save file loader data (dir)
    } else {
      core.osd[pos].prev_val = core.osd[pos].val;
      ffile.seek(FILE_POS_SWITCHES_DATA + pos);
      ffile.write(core.osd[pos].val);
    }
    ffile.close();
  } else {
    sd1.chvol();
    if (!file1.open(core.filename, FILE_WRITE)) {
      d_println("Unable to open bitstream file to write");
      return;
    }  
    if (!file1.isWritable()) {
      d_println("File is not writable");
      return;
    }
    core.osd_need_save = false;
    if (core.osd[pos].type == CORE_OSD_TYPE_FILEMOUNTER) {
      // todo: save file mounter data (dir, filename)
    } else if (core.osd[pos].type == CORE_OSD_TYPE_FILELOADER) {
      // todo save file loader data (dir)
    } else {
      core.osd[pos].prev_val = core.osd[pos].val;
      file1.seek(FILE_POS_SWITCHES_DATA + pos);
      file1.write(core.osd[pos].val);
    }
    file1.close();
  }
}

void app_core_on_keyboard() {

  if (is_filebrowser) {

    // down
    if (usb_keyboard_report.keycode[0] == KEY_DOWN || (joyL & SC_BTN_DOWN) || (joyR & SC_BTN_DOWN)) {
      if (filebrowser_len > 0) {
        if (filebrowser_sel < filebrowser_len-1) {
          filebrowser_sel++;
          //d_printf("New sel: %d", filebrowser_sel); d_println();
        } else {
          //filebrowser_sel = 0;
          if (filebrowser_total > 16 && filebrowser_offset < filebrowser_total-1) {
            filebrowser_offset++;
            //d_printf("New offset: %d", filebrowser_offset); d_println();
          }
        }
      }
      need_redraw = true;
    }

    // up
    if (usb_keyboard_report.keycode[0] == KEY_UP || (joyL & SC_BTN_UP) || (joyR & SC_BTN_UP)) {
      if (filebrowser_sel > 0) {
        filebrowser_sel--;
        //d_printf("New sel: %d", filebrowser_sel); d_println();
      } else {
        if (filebrowser_offset > 0) filebrowser_offset--;
        //d_printf("New offset: %d", filebrowser_offset); d_println();
        //filebrowser_sel = filebrowser_len-1;
      }
      need_redraw = true;
    }

    // enter
    if (usb_keyboard_report.keycode[0] == KEY_ENTER || (joyL & SC_BTN_B) || (joyR & SC_BTN_B) ) {
      // todo: if dir - enter dir ?
      // todo: if not dir - save selection, mount and close
      if (filebrowser_len > 0) {
        if (filebrowser_files[filebrowser_sel].is_dir) {
          // enter directory
          //d_printf("Enter %s dir", filebrowser_files[filebrowser_sel].dir); d_println();
          strcpy(file_slots[core.osd[curr_osd_item].slot_id].dir, filebrowser_files[filebrowser_sel].dir);
          filebrowser_offset = 0;
          filebrowser_sel = 0;
        } else {
          d_printf("Selecting file %s / %s", filebrowser_files[filebrowser_sel].dir, filebrowser_files[filebrowser_sel].filename); d_println();
          is_filebrowser = false;
          strcpy(file_slots[core.osd[curr_osd_item].slot_id].dir, filebrowser_files[filebrowser_sel].dir);
          strcpy(file_slots[core.osd[curr_osd_item].slot_id].filename, filebrowser_files[filebrowser_sel].filename);
          file_slots[core.osd[curr_osd_item].slot_id].is_mounted = true;
          // todo: update core with selection
          core.osd_need_save = true;

          if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILELOADER) {
            // send ioctl slot id, file data for file loader type
            spi_send(CMD_IOCTL_SLOT, 0, core.osd[curr_osd_item].slot_id);

            uint64_t fsize = 0;
            spi_send64(CMD_IOCTL_SIZE, fsize);
            String fname = String(file_slots[core.osd[curr_osd_item].slot_id].dir) + "/" + String(file_slots[core.osd[curr_osd_item].slot_id].filename);
            File32 file = sd1.open(fname);
            fsize = file.size();
            spi_send64(CMD_IOCTL_SIZE, fsize);

            String ext = fname.substring(fname.lastIndexOf('.')+1);
            for(uint8_t i = 0; i<ext.length(); i++) {
              spi_send(CMD_IOCTL_EXT, i, ext.charAt(i));
            }

            for(uint64_t i = 0; i < fsize; i++) {
              if (i % 256 == 0) {
                spi_send24(CMD_IOCTL_BANK, i >> 8);
              }
              uint8_t data = file.read();
              spi_send(CMD_IOCTL_DATA, (uint8_t)((i & 0x00000000000000FF)), data);
            }
            file.close();
          } else if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILEMOUNTER) {
            // send img slot id, size for file mounter type
            spi_send(CMD_IMG_SLOT, 0, core.osd[curr_osd_item].slot_id);
            uint64_t fsize = 0;
            spi_send64(CMD_IMG_SIZE, fsize);
            String fname = String(file_slots[core.osd[curr_osd_item].slot_id].dir) + "/" + String(file_slots[core.osd[curr_osd_item].slot_id].filename);
            File32 file = sd1.open(fname);
            fsize = file.size();
            spi_send64(CMD_IMG_SIZE, fsize);
            file.close();
          }
        }
      }
      need_redraw = true;
    }

    // esc
    if (usb_keyboard_report.keycode[0] == KEY_ESC || (joyL & SC_BTN_A) || (joyR & SC_BTN_A) ) {
      is_filebrowser = false;
      need_redraw = true;
    }

  } else {

        // down
        if (usb_keyboard_report.keycode[0] == KEY_DOWN || (joyL & SC_BTN_DOWN) || (joyR & SC_BTN_DOWN)) {
          curr_osd_item = find_next_item();
          need_redraw = true;
        }

        // up
        if (usb_keyboard_report.keycode[0] == KEY_UP || (joyL & SC_BTN_UP) || (joyR & SC_BTN_UP)) {
          curr_osd_item = find_prev_item();
          need_redraw = true;
        }

        // right, enter
        if (usb_keyboard_report.keycode[0] == KEY_RIGHT || usb_keyboard_report.keycode[0] == KEY_ENTER  || (joyL & SC_BTN_A) || (joyR & SC_BTN_A) || (joyL & SC_BTN_B) || (joyR & SC_BTN_B) || (joyL & SC_BTN_RIGHT) || (joyR & SC_BTN_RIGHT) ) {
          if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILELOADER) {
            is_filebrowser = true;
          } else {
            core.osd[curr_osd_item].val++; 
            if (core.osd[curr_osd_item].val > core.osd[curr_osd_item].options_len-1) {
              core.osd[curr_osd_item].val = 0;
            }
            core_send(curr_osd_item);
            if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_SWITCH) {
              core.osd_need_save = true;
            }
          }
          need_redraw = true;
        }

        // left
        if (usb_keyboard_report.keycode[0] == KEY_LEFT || (joyL & SC_BTN_LEFT) || (joyR & SC_BTN_LEFT)) {
          if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILELOADER) {
            is_filebrowser = true;
          } else {
            if (core.osd[curr_osd_item].val > 0) {
              core.osd[curr_osd_item].val--;
            } else {
              core.osd[curr_osd_item].val = core.osd[curr_osd_item].options_len-1;
            }
            core_send(curr_osd_item);
            if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_SWITCH) {
              core.osd_need_save = true;
            }
          }
          need_redraw = true;
        }
  }

  if (need_redraw) {
    need_redraw = false;
    // cls screen while changing screen mode
    if (prev_is_filebrowser != is_filebrowser) {
      prev_is_filebrowser = is_filebrowser;
      zxosd.fill(0, APP_COREBROWSER_MENU_OFFSET, 31, APP_COREBROWSER_MENU_OFFSET + 16, ' ');
    }
    if (is_filebrowser) {
      app_core_filebrowser(APP_COREBROWSER_MENU_OFFSET);
    } else {
      app_core_menu(APP_COREBROWSER_MENU_OFFSET);
    }
  }

  if (core.osd_need_save) {
    app_core_save(curr_osd_item);
  }
}

void app_core_filebrowser_add_entity(const char* dir, const char* name, const char* filename, bool is_dir)
{
  //d_printf("Entity %d:  %s / %s / %d", filebrowser_len, dir, filename, is_dir); d_println();
  core_filebrowser_item_t entity;
  entity.is_dir = is_dir;
  strcpy(entity.dir, dir);
  strcpy(entity.name, name);
  strcpy(entity.filename, filename);
  filebrowser_files[filebrowser_len] = entity;
  filebrowser_len++;
}

void app_core_filebrowser(uint8_t vpos) {
  // todo:
  zxosd.setPos(0, vpos);
  zxosd.setColor(OSD::COLOR_YELLOW_I, OSD::COLOR_BLACK);
  zxosd.print("Please choose a file to mount:");
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.line(vpos+1);

  String dir = String(file_slots[core.osd[curr_osd_item].slot_id].dir);
  if (dir == "") { dir = "/"; }
  if (dir.charAt(0) != '/') { dir = '/' + dir; }
  dir.toCharArray(file_slots[core.osd[curr_osd_item].slot_id].dir, sizeof(file_slots[core.osd[curr_osd_item].slot_id].dir));
  String sfilename = String( file_slots[core.osd[curr_osd_item].slot_id].filename);
  String sfullname = dir + "/" + sfilename;

  filebrowser_len = 0;
  filebrowser_special = 0;
  if (filebrowser_offset == 0) {
    filebrowser_special++;
    app_core_filebrowser_add_entity("/", ".", "", true);
  }

  if (sd1.exists(dir) && dir != "/") {
    sd1.chdir(dir);
    if (filebrowser_offset < 2) {
      filebrowser_special++;
      uint8_t idx = dir.lastIndexOf("/");
      String parent = dir.substring(0, idx);
      if (parent == "") parent = "/";
      char prnt[256];
      parent.toCharArray(prnt, sizeof(prnt));
      app_core_filebrowser_add_entity(prnt, "..", "", true);
    }
  } else {
    dir = "/";
    sd1.chdir(dir);
  }

  //d_printf("Offset: %d", filebrowser_offset); d_println();
  //d_printf("Special: %d", filebrowser_special); d_println();

  File32 root;
  File32 file;
  if (root.isOpen()) {
    root.close();
  }
  root = sd1.open(dir);
  uint8_t index = 0;
  if (root) {
    root.rewind();
    while (file.openNext(&root, O_RDONLY)) {
      if (index >= filebrowser_offset && index < filebrowser_offset + 16 - filebrowser_special) {
        char d[256];
        char name[32];
        char filename[256];
        dir.toCharArray(d, sizeof(d));
        file.getName(name, sizeof(name));
        file.getName(filename, sizeof(filename));
        if (file.isDir()) {
          char nd[256];
          String new_dir = dir + ((dir != "/") ? "/" : "") + filename;
          new_dir.toCharArray(nd, sizeof(nd));
          app_core_filebrowser_add_entity(nd, name, "", file.isDir());
        } else {
          app_core_filebrowser_add_entity(d, name, filename, file.isDir());
        }
      }
      file.close();
      index++;
    }
  }
  filebrowser_total = index;
  //d_printf("Total filebrowser len %d", filebrowser_len); d_println();

  if (filebrowser_len > 0) {
    for (uint8_t i=0; i<filebrowser_len; i++) {
      if (filebrowser_sel == i) {
        zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLUE);
      } else {
        zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
      }
      zxosd.setPos(0, vpos + 2 + i);
      zxosd.print(filebrowser_files[i].name);
      uint8_t name_len = strlen(filebrowser_files[i].name);
      if (name_len < 32) {
        for (uint8_t j=0; j<32-name_len; j++) {
          zxosd.print(" ");
        }
      }
    }
  }

  // fill the space
  if (filebrowser_len < 16) {
    zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
    for (uint8_t i=filebrowser_len; i<16; i++) {
      zxosd.setPos(0, vpos + 2 + i);
      for (uint8_t j=0; j<32; j++) {
        zxosd.print(" ");
      }
    }
  }
}