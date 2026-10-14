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
-- EU, 2024, 2025, 2026
------------------------------------------------------------------------------------------------------------------*/

#include "main.h"
#include "hid_driver.h"
#include "tusb.h"
#include "pio_usb.h"
#include "ps2kbd.h"
#include "app_core_browser.h"
#include "diag/diag.h"

#ifdef __FREERTOS
#include <FreeRTOS.h>
#include <task.h>
#endif

RTC zxrtc;
volatile bool usb_host_ready = false;
EspSerial esp_serial;
ESP8266 wifi(esp_serial);

static ElapsedTimer led_timer;

// firmware init, runs on core0
static void karabas_setup()
{
  link_queue_init();

  hw_setup.debug_enabled = true;

  msc_init();

  //rp2040.wdt_begin(5000);

  joyL = joyR = 0;
  for (uint8_t i=0; i<MAX_USB_JOYSTICKS; i++) { joyUSB[i] = 0; }
  joyUSB_len = 0;

  for (uint8_t i=0; i<MAX_FILE_SLOTS; i++) { file_slots[i] = {0}; }

  // SPI0 to FPGA, incoming fpga commands
  link_begin();
  link_on(CMD_FLASHBOOT, [](uint8_t addr, uint8_t data) { flashboot(data); });
  link_on(CMD_UART, serial_data);
  link_on(CMD_ESP_UART, [](uint8_t addr, uint8_t data) { esp_serial.rx_queue_push(data); });
  link_on(CMD_RTC, [](uint8_t addr, uint8_t data) { zxrtc.setData(addr, data); });
  link_on(CMD_PS2_SCANCODE, ps2_command_receive);
  matrix_register_handlers();

  // FPGA bitstream loader
  pinMode(PIN_CONF_INIT_B, INPUT_PULLUP);
  pinMode(PIN_CONF_PRG_B, OUTPUT);
  pinMode(PIN_CONF_DONE, INPUT);
  pinMode(PIN_CONF_CLK, INPUT);
  pinMode(PIN_CONF_IO1, INPUT);

  // FT, SD2, MCU CS lines 
  pinMode(PIN_MCU_SPI_CS, OUTPUT); digitalWrite(PIN_MCU_SPI_CS, HIGH);
  pinMode(PIN_MCU_SD2_CS, OUTPUT); digitalWrite(PIN_MCU_SD2_CS, HIGH);
  pinMode(PIN_MCU_FT_CS, OUTPUT); digitalWrite(PIN_MCU_FT_CS, HIGH);

  // unused MCU-FPGA pins, peripheral reset lines
  board_init_pins();

  // I2C
  Wire.setSDA(PIN_I2C_SDA);
  Wire.setSCL(PIN_I2C_SCL);
  Wire.setClock(100000);
  Wire.begin();

#if WAIT_SERIAL
  while ( !Serial ) yield();   // wait for native usb
#endif

  serial_bridge_begin();
  d_println("Karabas Go RP2040 firmware");

  // buttons, leds, joysticks
  board_init_io();

  matrix_begin();
  oled_begin();

  zxrtc.begin(spi_send, on_time);
  zxosd.begin(spi_send);
  ft.begin(spi_send);

  storage_begin();

  // load usb joy drivers
  d_println("Loading usb hid joystick drivers");
  hid_drivers_load();
  hid_drivers_dump();

  // if btn2 pressed on boot - espose msc
  msc_check_boot();

  osd_state = state_main;

  if (!msc_active()) {
    // load setup
    load_setup();

    // load boot from SD or flashfs
    if (has_sd && sd1.exists(FILENAME_BOOT)) {  
      do_configure(FILENAME_BOOT);
    } else {
      halt("Boot file not found. System stopped");
    }

    // todo: move this code to the app_setup 
    d_println("ESP8266 Init");
    esp_serial.begin(spi_send);
    //wifi.init();

    //d_println("ESP8266 FW Version:");
    //d_println(wifi.getVersion());

    osd_state = state_core_browser;
    app_core_browser_read_list();
  }
}

// usb host init, runs on core1
static void usb_host_setup() {
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;
  pio_cfg.pinout = (PIO_USB_PINOUT) board_usb_pinout();
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
  tuh_init(1);
  usb_host_ready = true;
}

// firmware main loop, runs on core0
static void karabas_loop()
{
  diag_loop_core0();
  diag_tick();

  //rp2040.wdt_reset();
  // read hw buttons to manipulate msc / reboots
  bool btn1 = btn_read(0);
  bool btn2 = btn_read(1);

  // in msc mode nothing else runs until eject or btn1 long press
  if (msc_tick(btn1)) return;

  zxrtc.handle();

  // osd hide / popup timers
  ui_tick();

  if (led_timer.elapsed() >= 100) {
      led_write(0, true);
      led_timer.reset();
  }

  input_tick_buttons(btn1, btn2);
  input_tick_joysticks();
  input_tick_repeat();

  osd_handle(false);

  link_report_drops();
  input_drain_events();

  serial_bridge_tick();

  // send byte from esp tx fifo
  esp_serial.handle();

  matrix_tick();
  oled_tick();
}

// usb host loop, runs on core1
static void usb_host_loop()
{
  diag_loop_core1();
  tuh_task();
}

#ifdef __FREERTOS

// FreeRTOS build (env *_rtos): the firmware and the usb host run in their own pinned tasks.
// The framework's CORE0 task (4 KB stack) only creates them; setup1/loop1 are not defined,
// so the framework does not start its own CORE1 task.

#ifndef APP_TASK_STACK
#define APP_TASK_STACK 4096 // words
#endif
#ifndef USB_TASK_STACK
#define USB_TASK_STACK 2048 // words
#endif
#define APP_TASK_PRIORITY (configMAX_PRIORITIES / 2)
#define USB_TASK_PRIORITY (configMAX_PRIORITIES - 2) // above everything but the framework's flash idle tasks

static void app_task(void *param) {
  karabas_setup();
  // no yield here: every scheduler call takes the SMP kernel lock with interrupts off,
  // which could delay the PIO-USB SOF interrupt on core1
  for (;;) {
    karabas_loop();
  }
}

static void usb_task(void *param) {
  usb_host_setup();
  for (;;) {
    usb_host_loop();
  }
}

void setup() {
  // tasks must be created already pinned: the USB task has a higher priority than the
  // framework's CORE0 task running this code, so it starts immediately on creation.
  // Unpinned, it could start on core0 and starve CORE0 forever (it never blocks),
  // and PIO-USB would set up its SOF timer on the wrong core.
  if (xTaskCreateAffinitySet(app_task, "APP", APP_TASK_STACK, nullptr, APP_TASK_PRIORITY, 1 << 0, nullptr) != pdPASS) {
    panic("Unable to create APP task");
  }
  if (xTaskCreateAffinitySet(usb_task, "USB", USB_TASK_STACK, nullptr, USB_TASK_PRIORITY, 1 << 1, nullptr) != pdPASS) {
    panic("Unable to create USB task");
  }
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}

#else

void setup() { karabas_setup(); }
void loop() { karabas_loop(); }
void setup1() { usb_host_setup(); }
void loop1() { usb_host_loop(); }

#endif

void halt(const char* msg) {
  d_println(msg);
  d_flush();
  bool blink = false;
  for(int i=0; i<50; i++) {
      blink = !blink;
      led_write(0, blink);
      led_write(1, !blink);
      delay(100);
  }
  rp2040.reboot();
}
