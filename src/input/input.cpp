#include "usb_joy.h"
#include "main.h"
#include "ps2kbd.h"
#include "usb_hid_keys.h"
#include "app_core.h"

#include <FreeRTOS.h>
#include <task.h>

// Input routing: on-board buttons, joysticks, usb/ps2 events from core1, global hotkeys.
//
// The INPUT task (core0, above APP) forwards usb/ps2/joystick events to the fpga as soon as they
// arrive, so they are not held back while APP is busy (OSD drawing, I2C, SD). Everything that needs the UI (hotkeys, OSD
// navigation) is handed to APP, which runs on_keyboard(). For a hotkey INPUT waits until APP has
// handled it before forwarding further keys, so e.g. Menu+Esc still opens the OSD without the
// Esc reaching the core.

volatile uint16_t joyL;
volatile uint16_t joyR;
static bool prev_kb_repeat_state = false;

enum {
  HOTKEY_NONE = -1,
  HOTKEY_OSD_TOGGLE = -2,
  HOTKEY_REBOOT = -3,
  // >= 0: index of the core OSD item with this hotkey
};

// which global hotkey (if any) the keyboard report / joysticks match
static int hotkey_find(const hid_keyboard_report_t &kbd, uint16_t jl, uint16_t jr) {
  // menu+esc (joy start+c) to toggle osd only for osd supported cores
  if (core.type != CORE_TYPE_BOOT && core.type != CORE_TYPE_HIDDEN &&
      ((((kbd.modifier & KEY_MOD_LMETA) || (kbd.modifier & KEY_MOD_RMETA)) && kbd.keycode[0] == KEY_ESC) ||
       ((jl & SC_BTN_START) && (jl & SC_BTN_C)) ||
       ((jr & SC_BTN_START) && (jr & SC_BTN_C))
      )
    ) {
    return HOTKEY_OSD_TOGGLE;
  }

  // ctrl+alt+backspace (joy start+x) to global reset rp2040
  if (
      (((kbd.modifier & KEY_MOD_LCTRL) || (kbd.modifier & KEY_MOD_RCTRL)) &&
      ((kbd.modifier & KEY_MOD_LALT) || (kbd.modifier & KEY_MOD_RALT)) &&
        kbd.keycode[0] == KEY_BACKSPACE) ||
        (((jl & SC_BTN_START) && (jl & SC_BTN_X))) ||
        (((jr & SC_BTN_START) && (jr & SC_BTN_X)))
        ) {
    return HOTKEY_REBOOT;
  }

  // osd hotkey
  for (uint8_t i=0; i<core.osd_len; i++) {
    // modifier matches (or the hotkey has no modifier)
    if ((core.osd[i].keys[0] != 0 && (kbd.modifier & core.osd[i].keys[0])) || (core.osd[i].keys[0] == 0)) {
      if (core.osd[i].keys[1] != 0 && (kbd.keycode[0] == core.osd[i].keys[1])) {
        return i;
      }
    }
  }
  return HOTKEY_NONE;
}

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

// read on-board joysticks and merge the usb ones; true if a side changed
static void joy_poll(bool &changedL, bool &changedR) {
  static uint16_t prevJoyL = 0;
  static uint16_t prevJoyR = 0;
  uint16_t l, r;
  board_joy_read(l, r);

  // merge the usb pads (HID and XInput share the two ports)
  uint16_t usb[2];
  usb_joy_snapshot(usb);
  l |= usb[0];
  r |= usb[1];

  joyL = l;
  joyR = r;

  changedL = (l != prevJoyL);
  changedR = (r != prevJoyR);
  prevJoyL = l;
  prevJoyR = r;
  if (changedL) { d_printf("SEGA L: %u", l); d_println(); }
  if (changedR) { d_printf("SEGA R: %u", r); d_println(); }
}

static void joy_send(bool changedL, bool changedR) {
  uint16_t l = joyL, r = joyR;
  if (changedL) {
    spi_send(CMD_JOYSTICK, 0, lowByte(l));
    spi_send(CMD_JOYSTICK, 1, highByte(l));
  }
  if (changedR) {
    spi_send(CMD_JOYSTICK, 2, lowByte(r));
    spi_send(CMD_JOYSTICK, 3, highByte(r));
  }
}

// forward one event from core1 to the fpga (keys, joysticks and the mouse are not forwarded while
// the OSD is open)
static void event_forward(const queue_spi_t &packet) {
  switch (packet.cmd) {
    case CMD_USB_KBD:
    case CMD_PS2_SCANCODE:
    case CMD_JOYSTICK:
    case CMD_USB_MOUSE:
      if (!is_osd) {
        spi_send(packet.cmd, packet.addr, packet.data);
      }
      break;
    default:
      spi_send(packet.cmd, packet.addr, packet.data);
  }
}

// use repeat state to fire on_keyboard event for osd repeating navigation
void input_tick_repeat() {
  if (is_osd && kb_repeat_key && kb_repeat_state && !prev_kb_repeat_state) {
    on_keyboard();
  }
  prev_kb_repeat_state = kb_repeat_state;
}


static TaskHandle_t input_task_handle = nullptr;
static volatile uint32_t ui_posted = 0;   // events handed to APP (written by INPUT)
static volatile uint32_t ui_done = 0;     // events handled by APP (written by APP)
static volatile bool input_waiting = false;

#define INPUT_TASK_STACK 1024 // words
#define INPUT_TASK_PRIORITY (configMAX_PRIORITIES / 2 + 1) // above APP
#define INPUT_ACK_TIMEOUT_MS 500

// hand an event to APP (it runs on_keyboard()); optionally wait until APP has handled it
static void post_to_app(bool wait) {
  uint32_t target = ++ui_posted;
  if (!wait) return;
  input_waiting = true;
  TickType_t start = xTaskGetTickCount();
  while ((int32_t)(target - ui_done) > 0 && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(INPUT_ACK_TIMEOUT_MS)) {
    ulTaskNotifyTake(pdTRUE, 1);
  }
  input_waiting = false;
}

static void input_route(const queue_spi_t &packet) {
  if (packet.cmd == CMD_USB_KBD && packet.addr == 1 && packet.data != 0) {
    // key press: APP runs on_keyboard(); for a hotkey wait for it, since it may open the OSD
    hid_keyboard_report_t kbd;
    hid_kbd_report_get(&kbd);
    bool hotkey = !is_osd && hotkey_find(kbd, joyL, joyR) != HOTKEY_NONE;
    post_to_app(hotkey);
  }
  event_forward(packet);
}

static void input_task(void *param) {
  for (;;) {
    // woken by spi_queue, or every tick to poll the joysticks
    ulTaskNotifyTake(pdTRUE, 1);
    if (msc_active()) {
      // no core is loaded in msc mode
      queue_spi_t packet;
      while (link_queue_pop(&packet)) {}
      continue;
    }

    bool changedL, changedR;
    joy_poll(changedL, changedR);
    if (changedL) post_to_app(false);
    if (changedR) post_to_app(false);
    joy_send(changedL, changedR);

    queue_spi_t packet;
    while (link_queue_pop(&packet)) {
      input_route(packet);
    }
  }
}

void input_task_start() {
  if (xTaskCreateAffinitySet(input_task, "INPUT", INPUT_TASK_STACK, nullptr, INPUT_TASK_PRIORITY, 1 << 0, &input_task_handle) != pdPASS) {
    panic("Unable to create INPUT task");
  }
  link_set_event_task(input_task_handle);
}

// APP side: run on_keyboard() for the events INPUT handed over, keep the keyboard led
void input_tick_ui() {
  while (ui_done != ui_posted) {
    on_keyboard();
    ui_done++;
  }
  if (input_waiting && input_task_handle) {
    xTaskNotifyGive(input_task_handle);
  }

  static bool prev_kbd_led = false;
  hid_kbd_report_sync();
  bool kbd_led = usb_keyboard_report.modifier != 0 || usb_keyboard_report.keycode[0] != 0;
  if (kbd_led != prev_kbd_led) {
    prev_kbd_led = kbd_led;
    led_write(1, kbd_led);
  }
}


bool on_global_hotkeys() {

  int hotkey = hotkey_find(usb_keyboard_report, joyL, joyR);

  if (hotkey == HOTKEY_OSD_TOGGLE) {
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

  if (hotkey == HOTKEY_REBOOT) {
     rp2040.reboot();
     return true;
  }

  if (hotkey >= 0) {
    uint8_t i = hotkey;
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
