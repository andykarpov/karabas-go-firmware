/*-------------------------------------------------------------------------------------------------------------------
-- 
-- 
-- #       #######                                                 #                                               
-- #                                                               #                                               
-- #                                                               #                                               
-- ############### ############### ############### ############### ############### ############### ############### 
-- #             #               # #                             # #             #               # #               
-- #             # ############### #               ############### #             # ############### ############### 
-- #             # #             # #               #             # #             # #             #               # 
-- #             # ############### #               ############### ############### ############### ############### 
--                                                                                                                 
--         ####### ####### ####### #######                                         ############### ############### 
--                                                                                 #               #             # 
--                                                                                 #   ########### #             # 
--                                                                                 #             # #             # 
-- https://github.com/andykarpov/karabas-go                                        ############### ############### 
--
-- RP2040 firmware for Karabas-Go
--
-- @author Andy Karpov <andy.karpov@gmail.com>
-- EU, 2024
------------------------------------------------------------------------------------------------------------------*/

#include <Arduino.h>
#include "config.h"
#include "types.h"
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
#include "bitmaps.h"
#include "osd_font.h"
#include "app_core_browser.h"
#include "app_file_loader.h"
#include "app_core.h"
#include "file.h"
#include <IniFile.h>
#include "hid_driver.h"

PioSpi spiSD(PIN_SD_SPI_RX, PIN_SD_SPI_SCK, PIN_SD_SPI_TX); // dedicated SD1 SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(30), &spiSD) // SD1 SPI Settings

SPISettings settingsA(SD_SCK_MHZ(4), MSBFIRST, SPI_MODE0); // MCU SPI settings
Adafruit_USBD_MSC usb_msc;

PCA9536 extender;
ElapsedTimer my_timer, my_timer2;
ElapsedTimer hide_timer;
ElapsedTimer popup_timer;
RTC zxrtc;
SdFat sd1;
FsFile file1, fileIndex1;
FsFile root1;
fs::Dir froot;
fs::File ffile;
OSD zxosd;

#if HW_ID == HW_ID_GO
SegaController sega;
FT812 ft(PIN_MCU_FT_CS, PIN_FT_RESET);
#elif HW_ID == HW_ID_MINI 
FT812 ft(PIN_MCU_FT_CS);
#endif

static queue_t spi_event_queue;

hid_keyboard_report_t usb_keyboard_report;
hid_mouse_report_t usb_mouse_report;

bool is_osd = false;
bool is_osd_hiding = false; 
bool is_popup_hiding = false;
bool need_redraw = false;

core_item_t core;
core_file_slot_t file_slots[4];

uint8_t osd_state;
uint8_t osd_prev_state = state_main;

hid_joy_config_t joy_drivers[255];
uint8_t joy_drivers_len;

bool has_extender = false;
bool has_sd = false;
bool has_fs = false;
bool has_ft = false;
bool is_flashboot = false;
bool is_configuring = false;
bool expose_msc = false;
bool ejected = false;
bool msc_blink = false;

uint16_t joyL;
uint16_t joyR;
uint16_t joyUSB[4];
uint8_t joyUSB_len;

uint8_t uart_idx = 0;
uint8_t evo_rs232_dll = 0;
uint8_t evo_rs232_dlm = 0;
uint32_t serial_speed = 115200;

uint16_t debug_address = 0;
uint16_t prev_debug_address = 0;
uint16_t debug_data = 0;
uint16_t prev_debug_data = 0;

void setup()
{
  queue_init(&spi_event_queue, sizeof(queue_spi_t), 64);

  usb_msc.setID(0, "Karabas", "SD Card", "1.0");
  usb_msc.setReadWriteCallback(0, msc_read_cb_sd, msc_write_cb_sd, msc_flush_cb_sd);
  usb_msc.setStartStopCallback(0, msc_start_stop_cb_sd);
  usb_msc.setUnitReady(0, false);
  usb_msc.begin();

  //rp2040.wdt_begin(5000);

  joyL = joyR = 0;
  for (uint8_t i=0; i<4; i++) { joyUSB[i] = 0; }
  joyUSB_len = 0;

  for (uint8_t i=0; i<4; i++) { file_slots[i] = {0}; }

  // SPI0 to FPGA
  SPI.setSCK(PIN_MCU_SPI_SCK);
  SPI.setRX(PIN_MCU_SPI_RX);
  SPI.setTX(PIN_MCU_SPI_TX);
  //SPI.setCS(PIN_MCU_SPI_CS);
  SPI.begin(false);

  // FPGA bitstream loader
  pinMode(PIN_CONF_INIT_B, INPUT_PULLUP);
  pinMode(PIN_CONF_PRG_B, OUTPUT);
  pinMode(PIN_CONF_DONE, INPUT);
  pinMode(PIN_CONF_CLK, OUTPUT);
  pinMode(PIN_CONF_IO1, OUTPUT);

  // FT, SD2, MCU CS lines 
  pinMode(PIN_MCU_SPI_CS, OUTPUT); digitalWrite(PIN_MCU_SPI_CS, HIGH);
  pinMode(PIN_MCU_SD2_CS, OUTPUT); digitalWrite(PIN_MCU_SD2_CS, HIGH);
  pinMode(PIN_MCU_FT_CS, OUTPUT); digitalWrite(PIN_MCU_FT_CS, HIGH);

  // I2C
  Wire.setSDA(PIN_I2C_SDA);
  Wire.setSCL(PIN_I2C_SCL);
  Wire.setClock(100000);
  Wire.begin();

#if WAIT_SERIAL
  while ( !Serial ) yield();   // wait for native usb
#endif

  d_begin(115200);
  d_println("Karabas Go RP2040 firmware");

#if HW_ID == HW_ID_GO
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
    extender.digitalWrite(PIN_EXT_LED1, HIGH);
    extender.digitalWrite(PIN_EXT_LED2, HIGH);
  }

  sega.begin(PIN_JOY_SCK, PIN_JOY_LOAD, PIN_JOY_DATA, PIN_JOY_P7);

#elif HW_ID == HW_ID_MINI
  has_extender = false;
  pinMode(PIN_BTN1, INPUT_PULLUP);
  pinMode(PIN_BTN2, INPUT_PULLUP);
  pinMode(PIN_LED1, OUTPUT); digitalWrite(PIN_LED1, HIGH);
  pinMode(PIN_LED2, OUTPUT); digitalWrite(PIN_LED2, HIGH);
#endif

  zxrtc.begin(spi_send, on_time);
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
  if (!sd1.begin(SD_CONFIG)) {
    has_sd = false;
    d_println("Failed");
  } else {
    has_sd = true;
    d_println("Done");
  }

  // load usb joy drivers
  d_println("Loading usb hid joystock drivers");
  hid_drivers_load();
  //hid_drivers_dump();

  // if btn2 pressed on boot - espose msc
  if (btn_read(1)) {
    if (has_sd) {
      expose_msc = true;
      ejected = false;
      uint32_t block_count = sd1.card()->sectorCount();
      usb_msc.setCapacity(0, block_count, 512);
      usb_msc.setUnitReady(0, true);
      my_timer.reset();
    }
  }

  osd_state = state_main;

  if (!expose_msc) {
    // load boot from SD or flashfs
    if (has_sd && sd1.exists(FILENAME_BOOT)) {  
      do_configure(FILENAME_BOOT);
    } else if (has_fs && LittleFS.exists(FILENAME_FBOOT)) {
      do_configure(FILENAME_FBOOT);
    } else {
      halt("Boot file not found. System stopped");
    }
    osd_state = state_core_browser;
    app_core_browser_read_list();
  }
}

void setup1() {
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;
#if HW_ID == HW_ID_GO
  pio_cfg.pinout = PIO_USB_PINOUT_DMDP;
#elif HW_ID == HW_ID_MINI
  pio_cfg.pinout = PIO_USB_PINOUT_DPDM;
#endif
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
  tuh_init(1);
}

void loop()
{
  //rp2040.wdt_reset();
  // read hw buttons to manipulate msc / reboots
  static bool prev_btn1 = false;
  static bool prev_btn2 = false;
  bool btn1 = btn_read(0);
  bool btn2 = btn_read(1);

  // if eject event registered - reboot rp2040
  if (ejected) {
    ejected = false;
    rp2040.reboot();
  }

  // if msc mounted but btn1 pressed - also reboot rp2040
  if (expose_msc) {

    if (my_timer2.elapsed() >= 200) {
          msc_blink = !msc_blink;
        led_write(1, msc_blink);
        my_timer2.reset();
    }

    if (btn1 && my_timer.elapsed() > 10000) {
      // wait for btn1 to release
      while(btn1) { btn1 = btn_read(0); delay(100); }
      expose_msc = false;
      rp2040.reboot();
    }

    // do not process other loop things until exit from msc mode
    return;
  }

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

  if (my_timer.elapsed() >= 100) {
      led_write(0, true);
      my_timer.reset();
  }

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

  // debug features
  // TODO: remove from production / refactor
  if (btn1) {
    // wait until released
    while (btn1) { btn1 = btn_read(0); delay(100); }
    d_println("Reboot");
    d_flush();
    rp2040.reboot();
  }
  /*if (btn2) {
      d_println("Reboot to bootloader");
      d_flush();
      rp2040.rebootToBootloader();
  }*/

  // joy reading
#if HW_ID == HW_ID_GO
  joyL = sega.getState(true);
  joyR = sega.getState(false);
#elif HW_ID == HW_ID_MINI
  joyL = 0;
  joyR = 0;
#endif
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

  osd_handle(false);

  queue_spi_t packet;
	while (queue_try_remove(&spi_event_queue, &packet)) {
    // skip keyboard transmission when osd is active
    if (packet.cmd == CMD_USB_KBD) {
#if HW_ID==HW_ID_GO
      if (has_extender) extender.digitalWrite(PIN_EXT_LED2, !(usb_keyboard_report.modifier != 0 || usb_keyboard_report.keycode[0] != 0));
#elif HW_ID==HW_ID_MINI
      digitalWrite(PIN_LED2, !(usb_keyboard_report.modifier != 0 || usb_keyboard_report.keycode[0] != 0));
#endif
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

  if (Serial.available() > 0) {
    uart_idx++;
    int uart_rx = Serial.read();
    if (uart_rx != -1) {
      spi_send(CMD_UART, uart_idx, (uint8_t) uart_rx);
    }
  } else {
    spi_send(CMD_NOP, 0 ,0);
  }

  if (core.type == CORE_TYPE_BOOT && has_ft == true && is_osd == true) {
    // todo: playSound
    // todo: timer
  }

}

void loop1()
{
   tuh_task();
}

void do_configure(const char* filename) {
  is_configuring = true;
  ft.vga(false);
  ft.spi(false);
  fpga_send(filename);
  spi_send(CMD_INIT_START, 0, 0);
  // trigger font loader reset
  zxosd.fontReset();
  // send font data
  for (int i=0; i<OSD_FONT_SIZE; i++) {
    zxosd.fontSend(osd_font[i]);
  }
  read_core(filename);
  if (!is_osd) {
    zxosd.clear();
    zxosd.logo(0,0, HW_ID);
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
  is_flashboot = false;
  is_configuring = false;
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
    if (core.osd[i].keys[0] != 0 && (usb_keyboard_report.modifier & core.osd[i].keys[0]) || (core.osd[i].keys[0] == 0)) {
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
              is_popup_hiding = true;
              popup_timer.reset();
              zxosd.showPopup();
            }
            if (core.osd[i].type == CORE_OSD_TYPE_SWITCH) {
              core.osd_need_save = true;
            }
          }
        } else if (core.osd[i].type == CORE_OSD_TYPE_TRIGGER) {
            core_trigger(i);
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

  need_redraw = on_global_hotkeys();

  // in-osd keyboard handle
  if (is_osd) {
    switch (osd_state) {
      case state_core_browser: app_core_browser_on_keyboard(); break;
      case state_main: app_core_on_keyboard(); break;
      case state_file_loader: app_file_loader_on_keyboard(); break;
    }
  }
}

void core_send(uint8_t pos)
{
  if (core.osd[pos].type == CORE_OSD_TYPE_FILEMOUNTER) {
      spi_send(CMD_SWITCHES, pos, (file_slots[core.osd[pos].slot_id].is_mounted) ? 1 : 0);
      d_printf("File mounter: %s %d", core.osd[pos].name, file_slots[core.osd[pos].slot_id].is_mounted);
  } else if (core.osd[pos].type == CORE_OSD_TYPE_FILELOADER) {
    // do nothing
  } else {
    spi_send(CMD_SWITCHES, pos, core.osd[pos].val);
    d_printf("Switch: %s %d", core.osd[pos].name, core.osd[pos].val);
  }
  d_println();
}

void core_send_all() 
{
  for (uint8_t i=0; i<core.osd_len; i++) {
    if (core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER) {
      spi_send(CMD_SWITCHES, i, (file_slots[core.osd[i].slot_id].is_mounted) ? 1 : 0);
    } else if (core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
      // do nothing
    } else {
      spi_send(CMD_SWITCHES, i, core.osd[i].val);
    }
  }
}

void core_trigger(uint8_t pos)
{
  d_printf("Trigger: %s", core.osd[pos].name);
  d_println();

  // reset FT812 on soft reset
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

void halt(const char* msg) {
  d_println(msg);
  d_flush();
  bool blink = false;
  while(true) {
      blink = !blink;
      led_write(0, blink);
      led_write(1, blink);
      delay(100);
  }
}

uint32_t fpga_send(const char* filename) {

  d_print("Configuring FPGA by "); d_println(filename);

  bool is_flash = is_flashfs(filename);

  if (is_flash) {
    ffile = LittleFS.open(filename, "r");
   } else {
    sd1.chvol();
    if (!file1.open(filename, FILE_READ)) {
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
      led_write(0, blink);
      led_write(1, blink);
    }
  }
  if (is_flash) {
    ffile.close();
  } else {
    file1.close();
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

void spi_send(uint8_t cmd, uint8_t addr, uint8_t data) {
  SPI.beginTransaction(settingsA);
  digitalWrite(PIN_MCU_SPI_CS, LOW);
  uint8_t rx_cmd = SPI.transfer(cmd);
  uint8_t rx_addr = SPI.transfer(addr);
  uint8_t rx_data = SPI.transfer(data);
  digitalWrite(PIN_MCU_SPI_CS, HIGH);
  SPI.endTransaction();
  if ((rx_cmd > 0) && !is_configuring) {
    process_in_cmd(rx_cmd, rx_addr, rx_data);
  }
}

void flashboot (uint8_t data) {
  uint8_t flashboot_coreid = data;
  d_printf("Flashboot core id: %02x", data); d_println();
  d_flush(); delay(100);
  // todo: add flashboot_id to the core structure, search for desired id and boot it
  // now it's implemented as hack: reload the same core (required for zx next hard reset)
  d_printf("Core id %s", core.id); d_println();
  d_printf("Core filename %s", core.filename); d_println();
  //String f = String(core.filename); f.trim(); 
  //d_printf("Loading core %s", f); d_println();
  //char buf[33]; f.toCharArray(buf, sizeof(buf));
  is_flashboot = true;
  do_configure(core.filename);
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

void process_in_cmd(uint8_t cmd, uint8_t addr, uint8_t data) {

  if (cmd == CMD_FLASHBOOT && !is_flashboot && strcmp(core.id, "zxnext")==0) {
    flashboot(data);
  } else if (cmd == CMD_UART) {
    serial_data(addr, data);
  } else if (cmd == CMD_RTC) {
    zxrtc.setData(addr, data);
  } else if (cmd == CMD_DEBUG_DATA) {
    debug_data = addr*256 + data;
  } else if (cmd == CMD_DEBUG_ADDRESS) {
    debug_address = addr*256 + data;
  } else if (cmd == CMD_NOP) {
    //d_println("Nop");
  }
  if (prev_debug_data != debug_data) {
    prev_debug_data = debug_data;
    d_printf("Debug: %04x", debug_data); d_println();
  }
}

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

  if (core.type == CORE_TYPE_BOOT && has_ft == true && is_osd == true) {
    // redraw core browser
    app_core_browser_ft_menu(0);
  }
}

void read_core(const char* filename) {

  bool is_flash = is_flashfs(filename);

  if (is_flash) {
    ffile = LittleFS.open(filename, "r");
  } else {
    sd1.chvol();
    if (!file1.open(filename, FILE_READ)) {
      halt("Unable to open bitstream file to read");
    }
  }

  core.flash = is_flash;
  if (is_flash) {
    String s = String(ffile.fullName());
    s = "/" + s;
    s.toCharArray(core.filename, sizeof(core.filename));
  } else {
    file1.getName(core.filename, sizeof(core.filename));
  }
  core.filename[32] = '\0';
  file_seek(FILE_POS_CORE_NAME, is_flash); file_read_bytes(core.name, 32, is_flash); core.name[32] = '\0';
  uint8_t visible; file_seek(FILE_POS_CORE_VISIBLE, is_flash); visible = file_read(is_flash); core.visible = (visible > 0);
  file_seek(FILE_POS_CORE_ORDER, is_flash); core.order = file_read(is_flash);
  file_seek(FILE_POS_CORE_TYPE, is_flash); core.type = file_read(is_flash);
  // show OSD on boot (only for boot and fileloader cores)
  is_osd = false;
  switch (core.type) {
    case CORE_TYPE_BOOT: is_osd = true; break;
    case CORE_TYPE_OSD: is_osd = false; break;
    case CORE_TYPE_FILELOADER: is_osd = true; break;
    default: is_osd = false;
  }  
  d_print("Core type: ");
  switch (core.type) {
    case CORE_TYPE_BOOT: d_println("Boot"); break;
    case CORE_TYPE_OSD: d_println("Normal"); break;
    case CORE_TYPE_FILELOADER: d_println("Fileloader"); break;
    case CORE_TYPE_HIDDEN: d_println("Hidden"); break;
    default: d_println("Reserved");
  }
  core.bitstream_length = file_read32(FILE_POS_BITSTREAM_LEN, is_flash);
  file_seek(FILE_POS_CORE_ID, is_flash); file_read_bytes(core.id, 32, is_flash); core.id[32] = '\0';
  file_seek(FILE_POS_CORE_BUILD, is_flash); file_read_bytes(core.build, 8, is_flash); core.build[8] = '\0';
  file_seek(FILE_POS_CORE_EEPROM_BANK, is_flash); core.eeprom_bank = file_read(is_flash);
  file_seek(FILE_POS_RTC_TYPE, is_flash); core.rtc_type = file_read(is_flash);
  file_seek(FILE_POS_FILELOADER_DIR, is_flash); file_read_bytes(core.dir, 32, is_flash); core.dir[32] = '\0';
  file_seek(FILE_POS_FILELOADER_FILE, is_flash); file_read_bytes(core.last_file, 32, is_flash); core.last_file[32] = '\0';
  file_seek(FILE_POS_FILELOADER_EXTENSIONS, is_flash); file_read_bytes(core.file_extensions, 32, is_flash); core.file_extensions[32] = '\0';
  uint32_t roms_len = file_read32(FILE_POS_ROM_LEN, is_flash);
  uint32_t offset = FILE_POS_BITSTREAM_START + core.bitstream_length + roms_len;
  //d_print("OSD section: "); d_println(offset);
  file_seek(offset, is_flash); core.osd_len = file_read(is_flash);
  //d_print("OSD len: "); d_println(core.osd_len);
  
  for (uint8_t i=0; i<4; i++) { file_slots[i].is_mounted = false; }

  for (uint8_t i=0; i<core.osd_len; i++) {
    core.osd[i].type = file_read(is_flash);
    file_read(is_flash);
    file_read_bytes(core.osd[i].name, 16, is_flash); core.osd[i].name[16] = '\0';
    core.osd[i].def = file_read(is_flash);
    core.osd[i].val = core.osd[i].def;
    core.osd[i].prev_val = core.osd[i].def;

    // filemounter osd type:
    // loading initial dir, filename, extensions and trying to mount file, if any
    if (core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
      core.osd[i].options_len = 0;
      core.osd[i].slot_id = file_read(is_flash);
      file_slots[core.osd[i].slot_id].is_mounted = false;
      file_read_bytes(file_slots[core.osd[i].slot_id].ext, 256, is_flash); file_slots[core.osd[i].slot_id].ext[255] = '\0';
      file_read_bytes(file_slots[core.osd[i].slot_id].dir, 256, is_flash); file_slots[core.osd[i].slot_id].dir[255] = '\0';
      String dir = String(file_slots[core.osd[i].slot_id].dir);
      if (dir == "") { dir = "/"; }
      if (dir.charAt(0) != '/') { dir = '/' + dir; }
      dir.toCharArray(file_slots[core.osd[i].slot_id].dir, sizeof(file_slots[core.osd[i].slot_id].dir));
      file_read_bytes(file_slots[core.osd[i].slot_id].filename, 256, is_flash); file_slots[core.osd[i].slot_id].filename[255] = '\0';
      String sfilename = String( file_slots[core.osd[i].slot_id].filename);
      String sfullname = dir + "/" + sfilename;
      if (sfilename.length() > 0 && sd1.exists(sfullname)) {
        file_slots[core.osd[i].slot_id].is_mounted = true; //file_slots[core.osd[i].slot_id].file = sd1.open(sfullname, O_READ);
      }
    } 
      // otherwise - reading options structure
      else {
      core.osd[i].options_len = file_read(is_flash);
      if (core.osd[i].options_len > 8) {
        core.osd[i].options_len = 8; // something goes wrong
      } 
      for (uint8_t j=0; j<core.osd[i].options_len; j++) {
        file_read_bytes(core.osd[i].options[j].name, 16, is_flash); core.osd[i].options[j].name[16] = '\0';
      }
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
  core_send_all();

  // re-send rtc registers
  //zxrtc.sendAll();

  has_ft = false;

  // hw ft reset
  ft.reset();

  // boot core tries to use FT812 as osd handler
  if (core.type == CORE_TYPE_BOOT && is_osd) {
    if (FT_OSD == 1) {
      // space skip ft osd
      if (usb_keyboard_report.keycode[0] == KEY_SPACE) {
        d_println("Space pressed: skip FT81x detection, fallback to classic OSD");
      } else {
        ft.spi(true);
        has_ft = ft.init(0); // 640x480x57
        if (has_ft) {
          d_println("Found FT81x IC, switching to FT OSD");
          ft.vga(true);
        } else {
          d_println("FT81x IC did not found");
          ft.vga(false);
          ft.spi(false);
        }
      }
    }
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
    file1.close();
  }

  // mount slots
  for(uint8_t i=0; i<core.osd_len; i++) {
    if (core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER) {
      String dir = String(file_slots[core.osd[i].slot_id].dir);
      if (dir == "") { dir = "/"; }
      if (dir.charAt(0) != '/') { dir = '/' + dir; }
      dir.toCharArray(file_slots[core.osd[i].slot_id].dir, sizeof(file_slots[core.osd[i].slot_id].dir));
      String sfilename = String( file_slots[core.osd[i].slot_id].filename);
      String sfullname = dir + "/" + sfilename;
      if (sfilename.length() > 0 && sd1.exists(sfullname)) {
        file_slots[core.osd[i].slot_id].is_mounted = file_slots[core.osd[i].slot_id].file = sd1.open(sfullname, O_READ);
      }
      core_send(i);
    }
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
    sd1.chvol();
    if (!file1.open(filename, FILE_READ)) {
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

        uint32_t addr = rom_addr + i*256 + j;
        uint8_t data = buf[j];

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
    file1.close();
  }
}

void osd_handle(bool force) {
  if (is_osd || force) {
    if ((osd_prev_state != osd_state) || force) {
      osd_prev_state = osd_state;
      switch(osd_state) {
        case state_core_browser:
          if (FT_OSD == 1 && has_ft == true) {
            app_core_browser_ft_overlay();
          } else {
            app_core_browser_overlay();
          }
        break;
        case state_main:
          app_core_overlay();
        break;
        case state_file_loader:
          app_file_loader_overlay(true, false);
        break;
      }
    }
  }  
}

bool btn_read(uint8_t num) {
#if HW_ID == HW_ID_GO
  if (num == 0) {
    return !(has_extender ? extender.digitalRead(PIN_EXT_BTN1) : HIGH);
  } else {
    return !(has_extender ? extender.digitalRead(PIN_EXT_BTN2) : HIGH);
  }
#elif HW_ID == HW_ID_MINI
  if (num == 0) { 
    return !digitalRead(PIN_BTN1);
  } else {
    return !digitalRead(PIN_BTN2);
  }
#endif
}

void led_write(uint8_t num, bool on) {
#if HW_ID == HW_ID_GO
  if (num == 0) {
    extender.digitalWrite(PIN_EXT_LED1, !on);
  } else {
    extender.digitalWrite(PIN_EXT_LED2, !on);
  }
#elif HW_ID == HW_ID_MINI
  if (num == 0) { 
    digitalWrite(PIN_LED1, !on);
  } else {
    digitalWrite(PIN_LED2, !on);
  }
#endif
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and
// return number of copied bytes (must be multiple of block size)
int32_t msc_read_cb_sd (uint32_t lba, void* buffer, uint32_t bufsize)
{
  bool rc;
  rc = sd1.card()->readSectors(lba, (uint8_t*) buffer, bufsize/512);
  return rc ? bufsize : -1;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and 
// return number of written bytes (must be multiple of block size)
int32_t msc_write_cb_sd (uint32_t lba, uint8_t* buffer, uint32_t bufsize)
{
  bool rc;
  rc = sd1.card()->writeSectors(lba, buffer, bufsize/512);
  return rc ? bufsize : -1;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
void msc_flush_cb_sd (void)
{
  sd1.card()->syncDevice();
  //sd1.cacheClear();
}

bool msc_start_stop_cb_sd(uint8_t power_condition, bool start, bool load_eject) {
  //d_printf("Start stop msc %d %d %d", power_condition, start, load_eject);
  //d_println(); d_flush();
  (void) power_condition;

  if ( load_eject )
  {
    if (start)
    {
      // load disk storage
    }else
    {
      // unload disk storage
      if (expose_msc) {
        expose_msc = false;
        ejected = true;
      }
    }
  }

  return true;
}

/////////