#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "PioSpi.h"
#include <SparkFun_PCA9536_Arduino_Library.h>
#include "SdFat.h"
#include "LittleFS.h"
#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"
#include <ElapsedTimer.h>
#include <RTC.h>
#include <OSD.h>
#include <FT812.h>
#include <SegaController.h>
#include "hid_app.h"
#include "main.h"
#include "usb_hid_keys.h"
#include <algorithm>
#include <tuple>

PioSpi spi(PIN_SD_SPI_RX, PIN_SD_SPI_SCK, PIN_SD_SPI_TX);
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(60), &spi)
SPISettings settingsA(SD_SCK_MHZ(20), MSBFIRST, SPI_MODE0); // MCU SPI settings

PCA9536 extender;
ElapsedTimer my_timer;
ElapsedTimer hide_timer;
ElapsedTimer popup_timer;
RTC zxrtc;
SdFs sd;
FsFile file;  
FsFile root;
fs::Dir froot;
fs::File ffile;
SegaController sega;
OSD zxosd;
FT812 ft;

static queue_t spi_event_queue;

hid_keyboard_report_t usb_keyboard_report;
hid_mouse_report_t usb_mouse_report;

bool is_osd = false;
bool is_osd_hiding = false; 
bool is_popup_hiding = false;

core_list_item_t cores[MAX_CORES];
core_item_t core;
uint8_t cores_len = 0;
uint8_t core_sel = 0;
const uint8_t core_page_size = MAX_CORES_PER_PAGE;
uint8_t core_pages = 1;
uint8_t core_page = 1;

uint8_t osd_state = state_main;
uint8_t osd_prev_state = state_main;
uint8_t osd_rtc_state = state_rtc_dow;
uint8_t osd_prev_rtc_state = state_rtc_dow;
uint8_t curr_osd_item = 0;

bool has_extender = false;
bool has_sd = false;
bool has_fs = false;

uint8_t uart_idx = 0;
uint8_t evo_rs232_dll = 0;
uint8_t evo_rs232_dlm = 0;
uint32_t serial_speed = 115200;

// the setup function runs once when you press reset or power the board
void setup()
{
  queue_init(&spi_event_queue, sizeof(queue_spi_t), 64);

  // SPI0 to FPGA
  SPI.setSCK(PIN_MCU_SPI_SCK);
  SPI.setRX(PIN_MCU_SPI_RX);
  SPI.setTX(PIN_MCU_SPI_TX);
  SPI.setCS(PIN_MCU_SPI_CS);  
  SPI.begin(true);

  // FPGA bitstream loader
  pinMode(PIN_CONF_INIT_B, INPUT_PULLUP);
  pinMode(PIN_CONF_PRG_B, OUTPUT);
  pinMode(PIN_CONF_DONE, INPUT);
  pinMode(PIN_CONF_CLK, OUTPUT);
  pinMode(PIN_CONF_IO1, OUTPUT);

  // I2C
  delay(100);
  Wire.setSDA(PIN_I2C_SDA);
  Wire.setSCL(PIN_I2C_SCL);
  Wire.setClock(100000);
  Wire.begin();

  //while ( !Serial ) delay(10);   // wait for native usb

  d_begin(115200);
  d_println("Karabas Go RP2040 firmware");

  if (extender.begin() == false) {
    has_extender = false;
    d_println("PCA9536 unavailable. Please check soldering.");
  } else {
    d_println("I2C PCA9536 extender detected");
    has_extender = true;
  }

  if (has_extender) {
    extender.pinMode(PIN_EXT_BTN1, INPUT_PULLUP);
    extender.pinMode(PIN_EXT_BTN2, INPUT_PULLUP);
    extender.pinMode(PIN_EXT_LED1, OUTPUT);
    extender.pinMode(PIN_EXT_LED2, OUTPUT);
  }

  zxrtc.begin(spi_send, on_time);
  sega.begin(PIN_JOY_SCK, PIN_JOY_LOAD, PIN_JOY_DATA, PIN_JOY_P7);
  zxosd.begin(spi_send);
  ft.begin(spi_send);

  // LittleFS
  d_print("Mounting Flash... ");
  LittleFSConfig cfg;
  cfg.setAutoFormat(true);
  LittleFS.setConfig(cfg);
  if (!LittleFS.begin()) {
    has_fs = false;
    d_println("Failed");
  } else {
    has_fs = true;
    d_println("Done");
  }

  // SD
  d_print("Mounting SD card... ");
  if (!sd.begin(SD_CONFIG)) {
    has_sd = false;
    //sd.initErrorHalt(&Serial);
    d_println("Failed");
  } else {
    has_sd = true;
    d_println("Done");
  }

  //sd.ls(&Serial, LS_SIZE);

  // load boot
  if (has_sd && sd.exists(FILENAME_BOOT)) {  
    do_configure(FILENAME_BOOT);
  } else if (has_fs && LittleFS.exists(FILENAME_FBOOT)) {
    do_configure(FILENAME_FBOOT);
  } else {
    halt("Boot file not found. System stopped");
  }
  osd_state = state_core_browser;
  read_core_list();
  //osd_init_core_browser_overlay();
}

void do_configure(const char* filename) {
  fpga_configure(filename);
  spi_send(CMD_INIT_START, 0, 0);
  read_core(filename);
  if (!is_osd) {
    zxosd.clear();
    osd_print_logo(0,0);
    zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
    zxosd.setPos(0,5);
    zxosd.print("ROM ");
    zxosd.showPopup();
  }
  read_roms(filename);
  if (!is_osd) {
    zxosd.hidePopup();
    osd_handle(true); // reinit osd
  }
  spi_send(CMD_INIT_DONE, 0, 0);
}

void core_browser(uint8_t vpos) {
  //d_print("Cores: "); d_println(cores_len);
  core_pages = ceil((float)cores_len / core_page_size);
  //d_print("Pages: "); d_println(core_pages);
  core_page = ceil((float)(core_sel+1)/core_page_size);
  //d_print("Core page: "); d_println(core_page);
  uint8_t core_from = (core_page-1)*core_page_size;
  uint8_t core_to = core_page*core_page_size > cores_len ? cores_len : core_page*core_page_size;
  uint8_t core_fill = core_page*core_page_size;
  uint8_t pos = vpos;
  //d_print("From: "); d_print(core_from); d_print(" to: "); d_println(core_to);
  //d_print("Selected: "); d_println(core_sel);
  for(uint8_t i=core_from; i < core_to; i++) {
    //d_print(cores[i].name); d_println(cores[i].order);
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
  //d_print("Fill: "); d_println(core_fill);
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

void menu(uint8_t vpos) {
  for (uint8_t i=0; i<core.osd_len; i++) {
    String name = String(core.osd[i].name);
    zxosd.setPos(0, i+vpos);
    zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
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

bool on_global_hotkeys() {
  
  // menu+esc to toggle osd only for osd supported cores
  if (core.type != 0 && core.type != 255 && ((usb_keyboard_report.modifier & KEY_MOD_LMETA) || (usb_keyboard_report.modifier & KEY_MOD_RMETA)) && usb_keyboard_report.keycode[0] == KEY_ESC) {
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

  // ctrl+alt+backspace to global reset rp2040
  if (
      ((usb_keyboard_report.modifier & KEY_MOD_LCTRL) || (usb_keyboard_report.modifier & KEY_MOD_RCTRL)) && 
      ((usb_keyboard_report.modifier & KEY_MOD_LALT) || (usb_keyboard_report.modifier & KEY_MOD_RALT)) && 
        usb_keyboard_report.keycode[0] == KEY_BACKSPACE) {
     rp2040.reboot();
     return true;
  }

  // ctrl+alt+del to core reset
  /*if (
      ((usb_keyboard_report.modifier & KEY_MOD_LCTRL) || (usb_keyboard_report.modifier & KEY_MOD_RCTRL)) && 
      ((usb_keyboard_report.modifier & KEY_MOD_LALT) || (usb_keyboard_report.modifier & KEY_MOD_RALT)) && 
        usb_keyboard_report.keycode[0] == KEY_DELETE) {
     // todo: send reset somehow
     return true;
  }*/

  // osd hotkey
  for (uint8_t i=0; i<core.osd_len; i++) {
    if (core.osd[i].keys[0] != 0 && (usb_keyboard_report.modifier & core.osd[i].keys[0]) || (core.osd[i].keys[0] == 0)) {
      if (core.osd[i].keys[1] != 0 && (usb_keyboard_report.keycode[0] == core.osd[i].keys[1])) {
        if (core.osd[i].type == CORE_OSD_TYPE_SWITCH || core.osd[i].type == CORE_OSD_TYPE_NSWITCH) {
          if (core.osd[i].options_len > 0) {
            curr_osd_item = i;
            core.osd[i].val++; 
            if (core.osd[i].val > core.osd[i].options_len-1) {
              core.osd[i].val = 0;
            }
            core_osd_send(i);
            if (!is_osd) {
              zxosd.clear();
              osd_print_logo(0, 0);
              zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
              zxosd.setPos(0, 5);
              zxosd.print(core.osd[i].name);
              zxosd.setPos(0,6);
              zxosd.print(core.osd[i].options[core.osd[i].val].name);
              is_popup_hiding = true;
              popup_timer.reset();
              zxosd.showPopup();
            }
            if (core.osd[i].type == CORE_OSD_TYPE_SWITCH) {
              core.osd_need_save = true;
            }
          }
        } else if (core.osd[i].type == CORE_OSD_TYPE_TRIGGER) {
            core_osd_trigger(i);
        }
        return true;
      }
    }
  }

  if (core.osd_need_save) {
    core_osd_save(curr_osd_item);
  }

  return false;
}

// kbd event handler
void on_keyboard() {

  bool need_redraw = on_global_hotkeys();

  // in-osd keyboard handle
  if (is_osd) {
    switch (osd_state) {
      case state_core_browser:
      if (cores_len > 0) {
        // down
        if (usb_keyboard_report.keycode[0] == KEY_DOWN) {
          if (core_sel < cores_len-1) {
            core_sel++;
          } else {
            core_sel = 0;
          }
        }

        // up
        if (usb_keyboard_report.keycode[0] == KEY_UP) {
          if (core_sel > 0) {
            core_sel--;
          } else {
            core_sel = cores_len-1;
          }
        }

        // right
        if (usb_keyboard_report.keycode[0] == KEY_RIGHT) {
          if (core_sel + core_page_size <= cores_len-1) {
            core_sel += core_page_size;
          } else {
            core_sel = cores_len-1;
          }
        }

        // left
        if (usb_keyboard_report.keycode[0] == KEY_LEFT) {
          if (core_sel - core_page_size >= 0) {
            core_sel -= core_page_size;
          } else {
            core_sel = 0;
          }
        }
        
        // enter
        if (usb_keyboard_report.keycode[0] == KEY_ENTER) {
          String f = String(cores[core_sel].filename); f.trim(); 
          char buf[32]; f.toCharArray(buf, sizeof(buf));
          do_configure(buf);
          osd_state = (core.type == 0) ? state_core_browser : state_main;
        }
        // redraw core browser
        if (osd_state == state_core_browser) {
          core_browser(APP_COREBROWSER_MENU_OFFSET);
        }
      }
      break;

      case state_main:

        // down
        if (usb_keyboard_report.keycode[0] == KEY_DOWN) {
          if (curr_osd_item < core.osd_len-1 && core.osd[curr_osd_item+1].options_len > 0) {
            curr_osd_item++;
          } else {
            curr_osd_item = 0;
          }
          need_redraw = true;
        }

        // up
        if (usb_keyboard_report.keycode[0] == KEY_UP) {
          if (curr_osd_item > 0) {
            curr_osd_item--;
          } else {
            // get last item with options
            uint8_t last_osd_item = 0;
            for (uint8_t i = 0; i < core.osd_len; i++) {
              if (core.osd[i].options_len > 0) last_osd_item = i;
            }
            curr_osd_item = last_osd_item;
          }
          need_redraw = true;
        }

        // right, enter
        if (usb_keyboard_report.keycode[0] == KEY_RIGHT || usb_keyboard_report.keycode[0] == KEY_ENTER) {
          core.osd[curr_osd_item].val++; 
          if (core.osd[curr_osd_item].val > core.osd[curr_osd_item].options_len-1) {
            core.osd[curr_osd_item].val = 0;
          }
          core_osd_send(curr_osd_item);
          if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_SWITCH) {
            core.osd_need_save = true;
          }
          need_redraw = true;
        }

        // left
        if (usb_keyboard_report.keycode[0] == KEY_LEFT) {
          if (core.osd[curr_osd_item].val > 0) {
            core.osd[curr_osd_item].val--;
          } else {
            core.osd[curr_osd_item].val = core.osd[curr_osd_item].options_len-1;
          }
          core_osd_send(curr_osd_item);
          if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_SWITCH) {
            core.osd_need_save = true;
          }
          need_redraw = true;
        }

        if (need_redraw) {
          menu(APP_COREBROWSER_MENU_OFFSET);
        }

        if (core.osd_need_save) {
          core_osd_save(curr_osd_item);
        }
      break;

    }
  }
}

void core_osd_save(uint8_t pos)
{
  bool is_flash = is_flashfs(core.filename);
  //d_print("Core osd "); d_print(core.osd[pos].name); d_print("="); d_println(core.osd[pos].val);
  //d_print(core.filename); d_print(" type "); d_println(is_flash ? " flash" : " sd");

  if (is_flash) {
    ffile = LittleFS.open(core.filename, "r+"); // read+write
    core.osd_need_save = false;
    core.osd[pos].prev_val = core.osd[pos].val;
    ffile.seek(FILE_POS_SWITCHES_DATA + pos);
    ffile.write(core.osd[pos].val);
    ffile.close();
  } else {
    if (!file.open(core.filename, FILE_WRITE)) {
      d_println("Unable to open bitstream file to write");
      return;
    }  
    if (!file.isWritable()) {
      d_println("File is not writable");
      return;
    }
    core.osd_need_save = false;
    core.osd[pos].prev_val = core.osd[pos].val;
    file.seek(FILE_POS_SWITCHES_DATA + pos);
    file.write(core.osd[pos].val);
    file.close();
  }
}

void core_osd_send(uint8_t pos)
{
  spi_send(CMD_SWITCHES, pos, core.osd[pos].val);
  d_printf("Switch: %s %d", core.osd[pos].name, core.osd[pos].val);
  d_println();
}

void core_osd_send_all() 
{
  for (uint8_t i=0; i<core.osd_len; i++) {
    spi_send(CMD_SWITCHES, i, core.osd[i].val);
  }
}

void core_osd_trigger(uint8_t pos)
{
  d_printf("Trigger: %s", core.osd[pos].name);
  d_println();

  // reset FT812 on reset
  // todo: replace hardcode with something else
  if (memcmp(core.osd[pos].name, "Rese", 4) == 0) {
    d_println("Reset FT812");
    ft.reset();
    delay(100);
  }

  d_println("Reset Host");
  spi_send(CMD_SWITCHES, pos, 1);
  delay(100);
  spi_send(CMD_SWITCHES, pos, 0);
}

uint8_t core_eeprom_get(uint8_t pos) {
  return core.eeprom[pos].val;
}

void core_eeprom_set(uint8_t pos, uint8_t val) {
  core.eeprom[pos].val = val;
  core.eeprom_need_save = true;
}

void loop()
{
  zxrtc.handle();

  // set is_osd off after 200ms of real switching off 
  // to avoid esc keypress passes to the host
  if (is_osd_hiding && hide_timer.elapsed() >= 200) {
    is_osd = false;
    is_osd_hiding = false;
  }

  // hide popup after 500 ms
  if (is_popup_hiding && popup_timer.elapsed() >= 500) {
    is_popup_hiding = false;
    zxosd.hidePopup();
    osd_handle(true); // reinit osd
  }

  if (has_extender && my_timer.elapsed() >= 100) {
    extender.digitalWrite(2, LOW);
    my_timer.reset();
  }

  static bool prev_btn1 = HIGH;
  static bool prev_btn2 = HIGH;
  bool btn1 = has_extender ? extender.digitalRead(0) : HIGH;
  bool btn2 = has_extender ? extender.digitalRead(1) : HIGH;

  if (prev_btn1 != btn1) {
    d_print("Button 1: "); d_println((btn1 == LOW) ? "on" : "off");
    prev_btn1 = btn1;
    spi_send(CMD_BTN, 0, !btn1);
  }

  if (prev_btn2 != btn2) {
    d_print("Button 2: "); d_println((btn2 == LOW) ? "on" : "off");
    prev_btn2 = btn2;
    spi_send(CMD_BTN, 1, !btn2);
  }

  // debug features
  // TODO: remove from production / refactor
  if (btn1 == LOW) {
    d_println("Reboot");
    d_flush();
    rp2040.reboot();
  }
  /*if (btn2 == LOW) {
      d_println("Reboot to bootloader");
      d_flush();
      rp2040.rebootToBootloader();
  }*/

  // joy reading
  uint16_t joyL = sega.getState(true);
  uint16_t joyR = sega.getState(false);
  static uint16_t prevJoyL = 0;
  static uint16_t prevJoyR = 0;

  if (joyL != prevJoyL) {
    d_printf("SEGA L: %u", joyL); d_println();
    prevJoyL = joyL;
    spi_send(CMD_JOYSTICK, 0, static_cast<uint8_t>(joyL & 0x00FF));
    spi_send(CMD_JOYSTICK, 1, static_cast<uint8_t>(joyL & 0xFF00) >> 8);
  }
  if (joyR != prevJoyR) {
    d_printf("SEGA R: %u", joyR); d_println();
    prevJoyR = joyR;
    spi_send(CMD_JOYSTICK, 2, static_cast<uint8_t>(joyR & 0x00FF));
    spi_send(CMD_JOYSTICK, 3, static_cast<uint8_t>(joyR & 0xFF00) >> 8);
  }

  osd_handle(false);

  queue_spi_t packet;
	while (queue_try_remove(&spi_event_queue, &packet)) {
    // capture keyboard events to osd when active
    if (packet.cmd == CMD_USB_KBD) {
      if (has_extender) extender.digitalWrite(3, !(usb_keyboard_report.modifier != 0 || usb_keyboard_report.keycode[0] != 0));
      if (packet.addr == 1 && packet.data != 0) {
          on_keyboard();
      }
      if (!is_osd) {
        spi_send(packet.cmd, packet.addr, packet.data);
      }
    } else {
      spi_send(packet.cmd, packet.addr, packet.data);
    }
  }

  if (Serial.available() > 0) {
    uart_idx++;
    int uart_rx = Serial.read();
    if (uart_rx != -1) {
      spi_send(CMD_UART, uart_idx, (uint8_t) uart_rx);
    }
  } else {
    spi_send(CMD_NOP, 0 ,0);
  }
}

// core1's setup
void setup1() {
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;
  pio_cfg.pinout = PIO_USB_PINOUT_DMDP;
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
  tuh_init(1);
}

// core1's loop
void loop1()
{
   tuh_task();
}

/**
 * @brief Halt the program execution with message
 * 
 * @param msg 
 */
void halt(const char* msg) {
  d_println(msg);
  d_flush();
  bool blink = false;
  while(true) {
      blink = !blink;
      if (has_extender) {
        extender.digitalWrite(PIN_EXT_LED1, blink);
        extender.digitalWrite(PIN_EXT_LED2, blink);
      }
      delay(100);
  }
}

void file_seek(uint32_t pos, bool is_flash) {
  if (is_flash) {
    ffile.seek(pos);
  } else {
    file.seek(pos);
  }
}

uint8_t file_read(bool is_flash) {
  if (is_flash) {
    return ffile.read();
  } else {
    return file.read();
  }
}

size_t file_read_bytes(char *buf, size_t len, bool is_flash) {
  if (is_flash) {
    return ffile.readBytes(buf, len);
  } else {
    return file.readBytes(buf, len);
  }
}

int file_read_buf(char *buf, size_t len, bool is_flash) {
  if (is_flash) {
    return ffile.readBytes(buf, len);
  } else {
    return file.read(buf, len);
  }
}

uint16_t file_read16(uint32_t pos, bool is_flash) {
  file_seek(pos, is_flash);
  
  uint16_t res = 0;

  if (is_flash) {
    char buf[2] = {0};
    ffile.readBytes(buf, sizeof(buf));
    res = buf[1] + buf[0]*256;
  } else {
    uint8_t buf[2] = {0};
    file.readBytes(buf, sizeof(buf));
    res = buf[1] + buf[0]*256;
  }
  
  return res;
}

uint32_t file_read24(uint32_t pos, bool is_flash) {
  file_seek(pos, is_flash);

  uint32_t res = 0;

  if (is_flash) {
    char buf[3] = {0};
    ffile.readBytes(buf, sizeof(buf));
    res = buf[2] + buf[1]*256 + buf[0]*256*256;
  } else {
    uint8_t buf[3] = {0};
    file.readBytes(buf, sizeof(buf));
    res = buf[2] + buf[1]*256 + buf[0]*256*256;
  }

  return res;
}

uint32_t file_read32(uint32_t pos, bool is_flash) {
  file_seek(pos, is_flash);

  uint32_t res = 0;
  if (is_flash) {
    char buf[4] = {0};
    ffile.readBytes(buf, sizeof(buf));
    res = buf[3] + buf[2]*256 + buf[1]*256*256 + buf[0]*256*256*256;
  } else {
    uint8_t buf[4] = {0};
    file.readBytes(buf, sizeof(buf));
    res = buf[3] + buf[2]*256 + buf[1]*256*256 + buf[0]*256*256*256;
  }

  return res;
}

void file_get_name(char *buf, size_t len, bool is_flash) {
  if (is_flash) {
    String s = String(ffile.fullName());
    s = "/" + s;
    s.toCharArray(buf, len);
  } else {
    file.getName(buf, sizeof(buf));
  }
}

bool is_flashfs(const char* filename) {
  String s = String(filename);
  return (s.charAt(0) == '/');
}

/**
 * @brief FPGA bitstream uploader
 * 
 * @param filename 
 */
uint32_t fpga_configure(const char* filename) {

  d_print("Configuring FPGA by "); d_println(filename);

  bool is_flash = is_flashfs(filename);

  if (is_flash) {
    ffile = LittleFS.open(filename, "r");
   } else {
    if (!file.open(filename, FILE_READ)) {
      halt("Unable to open bitstream file to read");
    }
   }

  // get bitstream size
  uint32_t length = file_read32(FILE_POS_BITSTREAM_LEN, is_flash);
  d_print("Bitstream size: "); d_println(length, DEC);

  // seek to bitstream start
  file_seek(FILE_POS_BITSTREAM_START, is_flash);

  // pulse PROG_B
  digitalWrite(PIN_CONF_PRG_B, HIGH);
  digitalWrite(PIN_CONF_PRG_B, LOW);
  digitalWrite(PIN_CONF_PRG_B, HIGH);

  // wait for INIT_B = 0
  delay(10);

  my_timer.reset();

  int i = 0;
  bool blink = false;
  char line[128];
  uint8_t n;

  digitalWrite(PIN_CONF_CLK, LOW);

  while ((n = file_read_buf(line, (sizeof(line) < length ? sizeof(line) : length), is_flash ))) {
    i += n;
    length -=n;

    for (uint8_t s=0; s<n; s++) {
      uint8_t c = line[s];
      for (uint8_t j=0; j<8; ++j) {
        // Set bit of data
        gpio_put(PIN_CONF_IO1, (c & (1<<(7-j))) ? HIGH : LOW);
        // Latch bit of data by CCLK impulse
        gpio_put(PIN_CONF_CLK, HIGH);
        gpio_put(PIN_CONF_CLK, LOW);
      }
    }

    if (i % 8192 == 0) {
      blink = !blink;
      if (has_extender) {
        extender.digitalWrite(PIN_EXT_LED1, blink);
        extender.digitalWrite(PIN_EXT_LED2, blink);
      }
    }
  }
  if (is_flash) {
    ffile.close();
  } else {
    file.close();
  }

  d_print(i, DEC); d_println(" bytes done");
  d_print("Elapsed time: "); d_print(my_timer.elapsed(), DEC); d_println(" ms");
  d_flush();

  d_print("Waiting for CONF_DONE... ");
  while(digitalRead(PIN_CONF_DONE) == LOW) {
    delay(10);
  }
  d_println("Done");

  return length;
}

void spi_queue(uint8_t cmd, uint8_t addr, uint8_t data) {
  queue_spi_t packet;
  packet.cmd = cmd;
  packet.addr = addr;
  packet.data = data;
  queue_try_add(&spi_event_queue, &packet);
}

/**
 * @brief send a custom message to MCU via hw SPI
 * 
 * @param cmd command
 * @param addr address
 * @param data data
 */
void spi_send(uint8_t cmd, uint8_t addr, uint8_t data) {
  SPI.beginTransaction(settingsA);
  digitalWrite(PIN_MCU_SPI_CS, LOW);
  uint8_t rx_cmd = SPI.transfer(cmd);
  uint8_t rx_addr = SPI.transfer(addr);
  uint8_t rx_data = SPI.transfer(data);
  digitalWrite(PIN_MCU_SPI_CS, HIGH);
  SPI.endTransaction();
  if (rx_cmd > 0) {
    process_in_cmd(rx_cmd, rx_addr, rx_data);
  }
}

void flashboot (uint8_t data) {
  uint8_t flashboot_coreid = data;
  d_printf("Flashboot core id: %02x", data); d_println();
  d_flush(); delay(100);
  // todo: add flashboot_id to the core structure, search for desired id and boot it
  // now it's implemented as hach: reload the same core (required for zx next hard reset)
  String f = String(core.filename); f.trim(); 
  char buf[32]; f.toCharArray(buf, sizeof(buf));
  do_configure(buf);
}

void serial_set_speed(uint8_t dll, uint8_t dlm) {
  uint32_t speed = 0;
  if (dll == 0 && dlm == 0) {
    speed = 256000; // zx evo special case
  } else if (bitRead(dlm, 7) == 1) {
    // native atmega mode
    dlm = bitClear(dlm, 7);
    // (uint16)((DLM&0x7F)*256+DLL) = (691200/<скорость в бодах>)-1 
    speed = 691200 / ((dlm*256) + dll + 1);
  } else {
    // standard mode
    speed = 115200 / ((dlm*256) + dll);
  }
  // switch serial spseed
  if (serial_speed != speed) {
    serial_speed = speed;
    Serial.end();
    Serial.begin(speed);
  }
}

void serial_data(uint8_t addr, uint8_t data) {
    if (addr == 0) {
      // ts zifi 115200
      if (serial_speed != 115200) {
        serial_speed = 115200;
        Serial.end();
        Serial.begin(serial_speed);
      }
      Serial.write(data);
    } else if (addr == 1) {
      // evo rs232 dll
      evo_rs232_dll = data;
      serial_set_speed(evo_rs232_dll, evo_rs232_dlm);
    } else if (addr == 2) {
      // evo rs232 set dlm
      evo_rs232_dlm = data;
      serial_set_speed(evo_rs232_dll, evo_rs232_dlm);
    } else if (addr == 3) {
      // evo rs232 data
      Serial.write(data);
    }
}

/**
 * @brief Process incoming command from FPGA
 * 
 * @param cmd command
 * @param addr address
 * @param data data
 */
void process_in_cmd(uint8_t cmd, uint8_t addr, uint8_t data) {

  if (cmd == CMD_FLASHBOOT) {
    flashboot(data);
  } else if (cmd == CMD_UART) {
    serial_data(addr, data);
  } else if (cmd == CMD_RTC) {
    zxrtc.setData(addr, data);
  } else if (cmd == CMD_FT812) {
  } else if (cmd == CMD_FT812_DATA) {
    ft.setData(addr, data);
  } else if (cmd == CMD_NOP) {
    //d_println("Nop");
  }
}

/**
 * @brief RTC callback
 * 
 */
void on_time() {
  zxosd.setPos(24, 0);
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  static bool dots_blink = !dots_blink;
  String dots = String(dots_blink ? ':' : ' ');
  uint8_t h = zxrtc.getHour();
  uint8_t m = zxrtc.getMinute();
  uint8_t s = zxrtc.getSecond();
  if (h < 10) zxosd.print(0); zxosd.print(h);
  if (dots_blink) zxosd.print(":"); else zxosd.print(" ");
  if (m < 10) zxosd.print(0); zxosd.print(m);
  if (dots_blink) zxosd.print(":"); else zxosd.print(" ");
  if (s < 10) zxosd.print(0); zxosd.print(s);

  zxosd.setPos(24, 1);
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  uint8_t d = zxrtc.getDay();
  uint8_t mo = zxrtc.getMonth();
  uint8_t y = zxrtc.getYear();
  if (d < 10) zxosd.print(0); zxosd.print(d); zxosd.print("/");
  if (mo < 10) zxosd.print(0); zxosd.print(mo); zxosd.print("/");
  if (y < 10) zxosd.print(0); zxosd.print(y);
}

core_list_item_t get_core_list_item(bool is_flash) {
  core_list_item_t core;
  if (is_flash) {
    String s = String(ffile.fullName());
    s = "/" + s;
    s.toCharArray(core.filename, sizeof(core.filename));
    core.flash = true;
  } else {
    file.getName(core.filename, sizeof(core.filename));
    core.flash = false;
  }
  file_seek(FILE_POS_CORE_NAME, is_flash); file_read_bytes(core.name, 32, is_flash); core.name[32] = '\0';
  uint8_t visible; file_seek(FILE_POS_CORE_VISIBLE, is_flash); visible = file_read(is_flash); core.visible = (visible > 0);
  file_seek(FILE_POS_CORE_ORDER, is_flash); core.order = file_read(is_flash);
  file_seek(FILE_POS_CORE_TYPE, is_flash); core.type = file_read(is_flash);
  file_seek(FILE_POS_CORE_BUILD, is_flash); file_read_bytes(core.build, 8, is_flash); core.build[8] = '\0';
  return core;
}

void read_core_list() {

  // files from flash
  if (has_fs) {
    fs::Dir dir = LittleFS.openDir("/");
    dir.rewind();
    while (dir.next()) {
      if (dir.isFile()) {
        ffile = dir.openFile("r");
        char filename[32]; file_get_name(filename, sizeof(filename), true);
        uint8_t len = strlen(filename);
        if (strstr(strlwr(filename + (len - 4)), ".kg1")) {
          cores[cores_len] = get_core_list_item(true);
          cores_len++;
        }
        ffile.close();
      }
    }
  }

  // files from sd card
  if (has_sd) {
    if (!root.open("/")) {
      halt("open root");
    }
    root.rewind();
    while (file.openNext(&root, O_RDONLY)) {
      char filename[32]; file.getName(filename, sizeof(filename));
      uint8_t len = strlen(filename);
      if (strstr(strlwr(filename + (len - 4)), ".kg1")) {
        cores[cores_len] = get_core_list_item(false);
        cores_len++;
      }
      file.close();
    }
  }
  // sort by core order number
  std::sort(cores, cores + cores_len);
  // TODO
}

void read_core(const char* filename) {

  bool is_flash = is_flashfs(filename);

  if (is_flash) {
    ffile = LittleFS.open(filename, "r");
  } else {
    if (!file.open(filename, FILE_READ)) {
      halt("Unable to open bitstream file to read");
    }
  }

  core.flash = is_flash;
  if (is_flash) {
    String s = String(ffile.fullName());
    s = "/" + s;
    s.toCharArray(core.filename, sizeof(core.filename));
  } else {
    file.getName(core.filename, sizeof(core.filename));
  }
  core.filename[32] = '\0';
  file_seek(FILE_POS_CORE_NAME, is_flash); file_read_bytes(core.name, 32, is_flash); core.name[32] = '\0';
  uint8_t visible; file_seek(FILE_POS_CORE_VISIBLE, is_flash); visible = file_read(is_flash); core.visible = (visible > 0);
  file_seek(FILE_POS_CORE_ORDER, is_flash); core.order = file_read(is_flash);
  file_seek(FILE_POS_CORE_TYPE, is_flash); core.type = file_read(is_flash);
  is_osd = (core.type == CORE_TYPE_BOOT) ? true : false;
  //is_osd = true; // debug  
  d_print("Core type: ");
  switch (core.type) {
    case 0: d_println("Boot"); break;
    case 1: d_println("Normal"); break;
    case 255: d_println("Hidden"); break;
    default: d_println("Reserved");
  }
  core.bitstream_length = file_read32(FILE_POS_BITSTREAM_LEN, is_flash);
  file_seek(FILE_POS_CORE_ID, is_flash); file_read_bytes(core.id, 32, is_flash); core.id[32] = '\0';
  file_seek(FILE_POS_CORE_BUILD, is_flash); file_read_bytes(core.build, 8, is_flash); core.build[8] = '\0';
  file_seek(FILE_POS_CORE_EEPROM_BANK, is_flash); core.eeprom_bank = file_read(is_flash);
  file_seek(FILE_POS_RTC_TYPE, is_flash); core.rtc_type = file_read(is_flash);
  uint32_t roms_len = file_read32(FILE_POS_ROM_LEN, is_flash);
  uint32_t offset = FILE_POS_BITSTREAM_START + core.bitstream_length + roms_len;
  //d_print("OSD section: "); d_println(offset);
  file_seek(offset, is_flash); core.osd_len = file_read(is_flash);
  //d_print("OSD len: "); d_println(core.osd_len);
  for (uint8_t i=0; i<core.osd_len; i++) {
    core.osd[i].type = file_read(is_flash);
    core.osd[i].bits = file_read(is_flash);
    file_read_bytes(core.osd[i].name, 16, is_flash); core.osd[i].name[16] = '\0';
    core.osd[i].def = file_read(is_flash);
    core.osd[i].val = core.osd[i].def;
    core.osd[i].prev_val = core.osd[i].def;
    core.osd[i].options_len = file_read(is_flash);
    if (core.osd[i].options_len > 8) {
      core.osd[i].options_len = 8; // something goes wrong
    } 
    for (uint8_t j=0; j<core.osd[i].options_len; j++) {
      file_read_bytes(core.osd[i].options[j].name, 16, is_flash); core.osd[i].options[j].name[16] = '\0';
    }
    file_read_bytes(core.osd[i].hotkey, 16, is_flash); core.osd[i].hotkey[16] = '\0';
    core.osd[i].keys[0] = file_read(is_flash);
    core.osd[i].keys[1] = file_read(is_flash);
    file_read(is_flash); // reserved
    file_read(is_flash); 
    file_read(is_flash);
  }

  // read eeprom data from file (in case rombank = 4 and up)
  // 255 means no eeprom allowed by core
  if (core.eeprom_bank >= MAX_EEPROM_BANKS && core.eeprom_bank != NO_EEPROM_BANK) {
    for (uint8_t i=0; i<255; i++) {
      file_seek(FILE_POS_EEPROM_DATA + i, is_flash);
      core.eeprom[i].val = file_read(is_flash);
      core.eeprom[i].prev_val = core.eeprom[i].val;
    }
  }
  zxrtc.setEepromBank(core.eeprom_bank);
  zxrtc.setRtcType(core.rtc_type);
  zxrtc.sendAll();

  // read saved switches
  for(uint8_t i=0; i<core.osd_len; i++) {
    file_seek(FILE_POS_SWITCHES_DATA + i, is_flash);
    core.osd[i].val = file_read(is_flash);
    if (core.osd[i].val > core.osd[i].options_len-1) {
      core.osd[i].val = core.osd[i].def;
    }
    core.osd[i].prev_val = core.osd[i].val;
  }
  core_osd_send_all();

  // re-send rtc registers
  zxrtc.sendAll();

  // reset ft812 (nowait)
  ft.reset();

  if (core.type == 0) {
    // ft test
    ft.spi(true);
    ft.vga(true);
    
    ft.init(0); // 640x480

    ft.drawLogo();

    delay(3500);

    ft.vga(false);
    ft.spi(false);

    /*ft.beginDisplayList();
    ft.clear(FT81x_COLOR_RGB(0, 0, 0));
    ft.swapScreen();*/

    /*ft.beginDisplayList();
    ft.clear(FT81x_COLOR_RGB(255, 255, 255));
    ft.drawText(240, 150, 31, FT81x_COLOR_RGB(0, 0, 0), FT81x_OPT_CENTER, "Hello World\0");
    ft.drawText(240, 210, 22, FT81x_COLOR_RGB(0, 0, 0), FT81x_OPT_CENTER, "FT812 on Karabas-Go!\0");
    ft.drawCircle(120, 350, 40, FT81x_COLOR_RGB(255, 0, 0));
    ft.drawCircle(240, 350, 40, FT81x_COLOR_RGB(0, 255, 0));
    ft.drawCircle(360, 350, 40, FT81x_COLOR_RGB(0, 0, 255));
    ft.swapScreen();*/

    /*ft.setAudioVolume(64);

    for (uint8_t pitch = 36; pitch <= 44; pitch++) {
        ft.setSound(FT81x_SOUND_GLOCKENSPIEL, pitch);
        ft.playSound();
        //delay(500);
    }*/

  }

  // dump parsed OSD items
  /*for(uint8_t i=0; i<core.osd_len; i++) {
    d_printf("OSD %d: type: %d name: %s def: %d len: %d keys: [%d %d %d]", i, core.osd[i].type, core.osd[i].name, core.osd[i].def, core.osd[i].options_len, core.osd[i].keys[0], core.osd[i].keys[1], core.osd[i].keys[2]); 
    d_println();
    for (uint8_t j=0; j<core.osd[i].options_len; j++) {
      d_print(core.osd[i].options[j].name); d_print(", "); 
    } 
    d_println();
    d_print(core.osd[i].hotkey); d_println();
  }*/

  if (is_flash) {
    ffile.close();
  } else {
    file.close();
  }

  zxosd.hidePopup();
  if (is_osd) {
    zxosd.showMenu();
  } else {
    zxosd.hideMenu();
  }

  osd_handle(true);
}

void read_roms(const char* filename) {

  bool is_flash = is_flashfs(filename);

  if (is_flash) {
    ffile = LittleFS.open(filename, "r");
  } else {
    if (!file.open(filename, FILE_READ)) {
      halt("Unable to open bitstream file to read");
    }
  }

  uint32_t bitstream_length = file_read32(FILE_POS_BITSTREAM_LEN, is_flash);
  uint32_t roms_len = file_read32(FILE_POS_ROM_LEN, is_flash);
  //d_print("ROMS len "); d_println(roms_len);
  if (roms_len > 0) {
    spi_send(CMD_ROMLOADER, 0, 1);
  }
  uint32_t offset = 0;
  uint32_t rom_idx = 0;
  while (roms_len > 0) {
    uint32_t rom_len = file_read32(FILE_POS_BITSTREAM_START + bitstream_length + offset, is_flash);
    uint32_t rom_addr = file_read32(FILE_POS_BITSTREAM_START + bitstream_length + offset + 4, is_flash);
    //d_print("ROM #"); d_print(rom_idx); d_print(": addr="); d_print(rom_addr); d_print(", len="); d_println(rom_len);
    for (uint32_t i=0; i<rom_len/256; i++) {
      char buf[256];
      int c = file_read_bytes(buf, sizeof(buf), is_flash);
      for (int j=0; j<256; j++) {
        send_rom_byte(rom_addr + i*256 + j, buf[j]);
      }
      if (rom_len > 0) {
        zxosd.setPos(4,5+rom_idx);
        //uint8_t perc = ceil((float) i*256 * (100.0 / rom_len));
        zxosd.print(rom_idx+1); zxosd.print(": ");
        zxosd.printf("%05d", (i+1)*256); zxosd.print(" ");
      }
    }
    offset = offset + rom_len + 8;
    roms_len = roms_len - rom_len - 8;
    rom_idx++;
    if (roms_len > 0) {
      // next rom
      zxosd.setPos(0, 5+rom_idx);
      zxosd.print("ROM ");
    }
  }
  //delay(100);
  spi_send(CMD_ROMLOADER, 0, 0);

  if (is_flash) {
    ffile.close();
  } else {
    file.close();
  }
}

void send_rom_byte(uint32_t addr, uint8_t data) {
  // send rombank address every 256 bytes
  if (addr % 256 == 0) {
    uint8_t rombank3 = (uint8_t)((addr & 0xFF000000) >> 24);
    uint8_t rombank2 = (uint8_t)((addr & 0x00FF0000) >> 16);
    uint8_t rombank1 = (uint8_t)((addr & 0x0000FF00) >> 8);
    //d_printf("ROM bank %d %d %d", rombank1, rombank2, rombank3); d_println();
    spi_send(CMD_ROMBANK, 0, rombank1);
    spi_send(CMD_ROMBANK, 1, rombank2);
    spi_send(CMD_ROMBANK, 2, rombank3);
  }
  // send lower 256 bytes
  uint8_t romaddr = (uint8_t)((addr & 0x000000FF));
  spi_send(CMD_ROMDATA, romaddr, data);
}

void osd_print_logo(uint8_t x, uint8_t y)
{
  zxosd.setPos(x,y);
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);

  // karabas go logo
  zxosd.write(0xb0); zxosd.write(0xb1); // k
  zxosd.write(0xb2); zxosd.write(0xb3); // a
  zxosd.write(0xb4); zxosd.write(0xb5); // r
  zxosd.write(0xb2); zxosd.write(0xb3); // a
  zxosd.write(0xb6); zxosd.write(0xb7); // b
  zxosd.write(0xb2); zxosd.write(0xb3); // a
  zxosd.write(0xb8); zxosd.write(0xb9); // s

  zxosd.setPos(x,y+1);
  zxosd.write(0xc0); zxosd.write(0xc1); // k
  zxosd.write(0xc2); zxosd.write(0xc3); // a
  zxosd.write(0xc4); zxosd.write(0xc5); // r
  zxosd.write(0xc2); zxosd.write(0xc3); // a
  zxosd.write(0xc6); zxosd.write(0xc7); // b
  zxosd.write(0xc2); zxosd.write(0xc3); // a
  zxosd.write(0xc8); zxosd.write(0xc9); // s

  zxosd.setPos(x+10, y+2);
  zxosd.write(0xba); zxosd.write(0xbb); // g
  zxosd.write(0xbc); zxosd.write(0xbd); // o

  zxosd.setPos(x+10, y+3);
  zxosd.write(0xca); zxosd.write(0xcb); // g
  zxosd.write(0xcc); zxosd.write(0xcd); // o

  zxosd.setPos(x+1, y+2);
  zxosd.setColor(OSD::COLOR_RED_I, OSD::COLOR_BLACK);
  zxosd.write(0x16); // -
  zxosd.setColor(OSD::COLOR_YELLOW_I, OSD::COLOR_BLACK);
  zxosd.write(0x16); // -
  zxosd.setColor(OSD::COLOR_GREEN_I, OSD::COLOR_BLACK);
  zxosd.write(0x16); // -
  zxosd.setColor(OSD::COLOR_BLUE_I, OSD::COLOR_BLACK);
  zxosd.write(0x16); // -

  zxosd.setColor(OSD::COLOR_GREY, OSD::COLOR_BLACK);
  zxosd.setPos(x,y+3);
}

void osd_print_line(uint8_t y)
{
  zxosd.setPos(0,y);
  for (uint8_t i=0; i<32; i++) {
    zxosd.write(0x5f);
    //zxosd.write(0xd1); // 0xd1 - new
  }
}

void osd_print_space() {
  uint8_t i = 0;
  for (i=0; i<8; i++) {
    zxosd.print(" ");
  }
}

void osd_print_header()
{
  osd_print_logo(0,0);

  zxosd.setColor(OSD::COLOR_GREY, OSD::COLOR_BLACK);
  zxosd.setPos(19,2);
  zxosd.print("FPGA "); zxosd.print(core.build);

  zxosd.setColor(OSD::COLOR_GREY, OSD::COLOR_BLACK);
  zxosd.setPos(19,3);
  zxosd.print("CORE "); zxosd.print(core.id);

  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  osd_print_line(4);

  //updateTime();
}

void osd_init_overlay()
{
  osd_state = state_main;

  // disable popup in overlay mode
  zxosd.hidePopup();

  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();

  osd_print_header();

  menu(APP_COREBROWSER_MENU_OFFSET);

  // footer
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  osd_print_line(24);
  zxosd.setPos(0,25); zxosd.print("Press Menu+Esc to toggle OSD");
}

void osd_clear()
{
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();
}

void osd_init_popup(uint8_t event_type)
{
  // todo
}

// init rtc osd
void osd_init_rtc_overlay()
{
  // todo
}

// init about osd
void osd_init_about_overlay()
{
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();

  osd_print_header();

  zxosd.setPos(0,5);
  zxosd.print("About");

  osd_print_footer();

}

// init info osd
void osd_init_info_overlay()
{
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();

  osd_print_header();

  zxosd.setPos(0,5);
  zxosd.print("System Info");
  
  osd_print_footer();
}

void osd_print_footer() {

  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  osd_print_line(22);

  // footer
  zxosd.setPos(0,23); 
  zxosd.print("Press ESC to return");
}

void osd_init_core_browser_overlay() {
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.clear();

  osd_print_header();

  zxosd.setPos(0,5);

  core_browser(APP_COREBROWSER_MENU_OFFSET);  

  // footer
  osd_print_line(21);
  osd_print_line(23);
  zxosd.setPos(1,24); zxosd.print("Please use arrows to navigate");
  zxosd.setPos(1,25); zxosd.print("Press Enter to load selection");
}

void osd_handle(bool force) {
  if (is_osd || force) {
    if ((osd_prev_state != osd_state) || force) {
      osd_prev_state = osd_state;
      switch(osd_state) {
        case state_core_browser:
          osd_init_core_browser_overlay();
        break;
        case state_main:
          osd_init_overlay();
        break;
        case state_rtc:
          osd_init_rtc_overlay();
        break;
        case state_about:
          osd_init_about_overlay();
        break;
        case state_info:
          osd_init_info_overlay();
        break;
      }
    }
  }
}

bool operator<(const core_list_item_t a, const core_list_item_t b) {
  // Lexicographically compare the tuples (hour, minute)
  return a.order < b.order;
}

