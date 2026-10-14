#include "main.h"
#include "ps2kbd.h"
#include "usb_hid_keys.h"
#include "app_core.h"

// Input routing: on-board buttons, joysticks, usb/ps2 events from core1, global hotkeys.

uint16_t joyL;
uint16_t joyR;
static bool prev_kb_repeat_state = false;

void input_tick_buttons(bool btn1, bool btn2) {
  static bool prev_btn1 = false;
  static bool prev_btn2 = false;

  if (prev_btn1 != btn1) {
    d_print("Button 1: "); d_println((btn1) ? "on" : "off");
    prev_btn1 = btn1;
    spi_send(CMD_BTN, 0, btn1);
  }

  if (prev_btn2 != btn2) {
    d_print("Button 2: "); d_println((btn2) ? "on" : "off");
    prev_btn2 = btn2;
    spi_send(CMD_BTN, 1, btn2);
  }
}

void input_tick_joysticks() {
  // joy reading
  board_joy_read(joyL, joyR);
  static uint16_t prevJoyL = 0;
  static uint16_t prevJoyR = 0;

  // merge 
  if (joyUSB_len > 0) {
    joyL = joyL | joyUSB[0];
  }
  if (joyUSB_len > 1) {
    joyR = joyR | joyUSB[1];
  }

  if (joyL != prevJoyL) {
    d_printf("SEGA L: %u", joyL); d_println();
    on_keyboard();
    prevJoyL = joyL;
    spi_send(CMD_JOYSTICK, 0, lowByte(joyL));
    spi_send(CMD_JOYSTICK, 1, highByte(joyL));
  }
  if (joyR != prevJoyR) {
    d_printf("SEGA R: %u", joyR); d_println();
    on_keyboard();
    prevJoyR = joyR;
    spi_send(CMD_JOYSTICK, 2, lowByte(joyR));
    spi_send(CMD_JOYSTICK, 3, highByte(joyR));
  }
}

// use repeat state to fire on_keyboard event for osd repeating navigation
void input_tick_repeat() {
  if (is_osd && kb_repeat_key && kb_repeat_state && !prev_kb_repeat_state) {
    on_keyboard();
  }
  prev_kb_repeat_state = kb_repeat_state;
}

// forward events queued by core1 (and core0) to the fpga
void input_drain_events() {
  queue_spi_t packet;
  while (link_queue_pop(&packet)) {
    // skip keyboard transmission when osd is active
    if (packet.cmd == CMD_USB_KBD) {
      hid_kbd_report_sync();
      led_write(1, usb_keyboard_report.modifier != 0 || usb_keyboard_report.keycode[0] != 0);
      if (packet.addr == 1 && packet.data != 0) {
          on_keyboard();
      }
      if (!is_osd) {
        spi_send(packet.cmd, packet.addr, packet.data);
      }
    // skip ps/2 scancode transmission when osd is active
    } else if (packet.cmd == CMD_PS2_SCANCODE) {
      if (!is_osd) {
        spi_send(packet.cmd, packet.addr, packet.data);
      }
    // skip joystick transmission when osd is active
    } else if (packet.cmd == CMD_JOYSTICK) {
      if (!is_osd) {
        spi_send(packet.cmd, packet.addr, packet.data);
      }
    }
    else {
      spi_send(packet.cmd, packet.addr, packet.data);
    }
  }
}

bool on_global_hotkeys() {
  
  // menu+esc (joy start+c) to toggle osd only for osd supported cores
  if (core.type != CORE_TYPE_BOOT && core.type != CORE_TYPE_HIDDEN && 
      ((((usb_keyboard_report.modifier & KEY_MOD_LMETA) || (usb_keyboard_report.modifier & KEY_MOD_RMETA)) && usb_keyboard_report.keycode[0] == KEY_ESC) ||
       ((joyL & SC_BTN_START) && (joyL & SC_BTN_C)) ||
       ((joyR & SC_BTN_START) && (joyR & SC_BTN_C))
      )
    ) {
    if (!is_osd) {
      is_osd = true;
      zxosd.showMenu();
    } else if (!is_osd_hiding) {
      is_osd_hiding = true;
      hide_timer.reset();
      zxosd.hideMenu();
    }
    return true;
  }

  // ctrl+alt+backspace (joy start+x) to global reset rp2040
  if (
      (((usb_keyboard_report.modifier & KEY_MOD_LCTRL) || (usb_keyboard_report.modifier & KEY_MOD_RCTRL)) && 
      ((usb_keyboard_report.modifier & KEY_MOD_LALT) || (usb_keyboard_report.modifier & KEY_MOD_RALT)) && 
        usb_keyboard_report.keycode[0] == KEY_BACKSPACE) || 
        (((joyL & SC_BTN_START) && (joyL & SC_BTN_X))) ||
        (((joyR & SC_BTN_START) && (joyR & SC_BTN_X)))
        ) {
     rp2040.reboot();
     return true;
  }

  // osd hotkey
  for (uint8_t i=0; i<core.osd_len; i++) {
    // modifier matches (or the hotkey has no modifier)
    if ((core.osd[i].keys[0] != 0 && (usb_keyboard_report.modifier & core.osd[i].keys[0])) || (core.osd[i].keys[0] == 0)) {
      if (core.osd[i].keys[1] != 0 && (usb_keyboard_report.keycode[0] == core.osd[i].keys[1])) {
        if (core.osd[i].type == CORE_OSD_TYPE_SWITCH || core.osd[i].type == CORE_OSD_TYPE_NSWITCH) {
          if (core.osd[i].options_len > 0) {
            curr_osd_item = i;
            core.osd[i].val++; 
            if (core.osd[i].val > core.osd[i].options_len-1) {
              core.osd[i].val = 0;
            }
            core_send(i);
            if (!is_osd) {
              zxosd.clear();
              zxosd.logo(0, 0, HW_ID);
              zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
              zxosd.setPos(0, 5);
              zxosd.print(core.osd[i].name);
              zxosd.setPos(0,6);
              zxosd.print(core.osd[i].options[core.osd[i].val].name);
              zxosd.update();
              ui_popup_start(i);
            }
            if (core.osd[i].type == CORE_OSD_TYPE_SWITCH) {
              core.osd_need_save = true;
            }
          }
        } else if (core.osd[i].type == CORE_OSD_TYPE_TRIGGER) {
            core_trigger(i);
        } else if (core.osd[i].type == CORE_OSD_TYPE_FILELOADER || core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER) {
          is_osd = true;
          curr_osd_item = i;
          zxosd.showMenu();
        }
        return true;
      }
    }
  }

  if (core.osd_need_save) {
    app_core_save(curr_osd_item);
  }

  return false;
}

void on_keyboard() {

  hid_kbd_report_sync();

  need_redraw = on_global_hotkeys();

  // in-osd keyboard handle
  ui_on_keyboard();
}
