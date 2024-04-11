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

uint8_t find_first_item() {
  if (core.osd_len > 0) {
    for (uint8_t i = 0; i < core.osd_len; i++) {
      if (core.osd[i].options_len > 0) {
        return i;
      }
    }
  }
  return 0;
}

uint8_t find_last_item() {
  if (core.osd_len > 0) {
    for (uint8_t i = core.osd_len-1; i >= 0; i--) {
      if (core.osd[i].options_len > 0) {
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
      if (core.osd[i].options_len > 0) {
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
      if (core.osd[i].options_len > 0) {
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

      String option = String(core.osd[i].options[core.osd[i].val].name);
      zxosd.setPos(11, i+vpos);
      if (curr_osd_item == i) {
        zxosd.setColor(OSD::COLOR_BLACK, OSD::COLOR_YELLOW_I);
      } else {
        zxosd.setColor(OSD::COLOR_YELLOW_I, OSD::COLOR_BLACK);
      }
      zxosd.print(option.substring(0, 10));

      String hotkey = String(core.osd[i].hotkey);
      zxosd.setColor(OSD::COLOR_CYAN_I, OSD::COLOR_BLACK);
      if (core.osd[i].options_len > 0) {
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
    core.osd[pos].prev_val = core.osd[pos].val;
    ffile.seek(FILE_POS_SWITCHES_DATA + pos);
    ffile.write(core.osd[pos].val);
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
    core.osd[pos].prev_val = core.osd[pos].val;
    file1.seek(FILE_POS_SWITCHES_DATA + pos);
    file1.write(core.osd[pos].val);
    file1.close();
  }
}

void app_core_on_keyboard() {
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
          core.osd[curr_osd_item].val++; 
          if (core.osd[curr_osd_item].val > core.osd[curr_osd_item].options_len-1) {
            core.osd[curr_osd_item].val = 0;
          }
          core_send(curr_osd_item);
          if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_SWITCH) {
            core.osd_need_save = true;
          }
          need_redraw = true;
        }

        // left
        if (usb_keyboard_report.keycode[0] == KEY_LEFT || (joyL & SC_BTN_LEFT) || (joyR & SC_BTN_LEFT)) {
          if (core.osd[curr_osd_item].val > 0) {
            core.osd[curr_osd_item].val--;
          } else {
            core.osd[curr_osd_item].val = core.osd[curr_osd_item].options_len-1;
          }
          core_send(curr_osd_item);
          if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_SWITCH) {
            core.osd_need_save = true;
          }
          need_redraw = true;
        }

        if (need_redraw) {
          app_core_menu(APP_COREBROWSER_MENU_OFFSET);
        }

        if (core.osd_need_save) {
          app_core_save(curr_osd_item);
        }
}