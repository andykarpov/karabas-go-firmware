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
#include "diag/hang_diag.h"
#include "usb/printer_host.h"
#include "periph/printer_usb.h"
#include "periph/printer_link.h"

#ifndef __FREERTOS
#error "The firmware runs on FreeRTOS: build it with -D PIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS (see platformio.ini)"
#endif
#include <FreeRTOS.h>
#include <task.h>
// see [rp2040] in platformio.ini: wakeups from irq context go through the timer task
static_assert(configTIMER_TASK_PRIORITY == configMAX_PRIORITIES - 1, "FreeRTOS timer task must run above APP and USB");

RTC zxrtc;
volatile bool usb_host_ready = false;
EspSerial esp_serial;

static ElapsedTimer led_timer;

// Under FreeRTOS SysTick belongs to the scheduler, so the core runs its cycle counter (ccount,
// 2 instructions) on a PIO state machine, allocated in main() before setup(). It lands on pio1,
// whose whole 32-word instruction memory the PIO-USB receiver needs (decoder 15 + edge
// detector 17): the edge detector then fails to load and the host never sees a handshake.
// Reserve pio1 and pio0 sm0 (PIO-USB tx) in a static constructor, which runs before main(),
// so ccount goes to pio0 sm1. usb_host_setup() releases them right before PIO-USB init,
// which gives the usb host the PIO layout it expects.
static struct UsbPioReserve {
  UsbPioReserve() {
    pio_sm_claim(pio0, 0);
    pio_claim_sm_mask(pio1, 0xf);
  }
} usb_pio_reserve;

// firmware init, runs on core0
static void karabas_setup()
{
  // the boot log goes into the buffer until karabas.ini says whether debug is on
  hw_setup.debug_enabled = true;
  hang_diag_report();

  msc_init();

  //rp2040.wdt_begin(5000);

  joyL = joyR = 0;

  for (uint8_t i=0; i<MAX_FILE_SLOTS; i++) { file_slots[i] = {0}; }

  // SPI0 to FPGA, incoming fpga commands
  link_begin();
  link_on(CMD_FLASHBOOT, [](uint8_t addr, uint8_t data) { flashboot(data); });
  link_on(CMD_UART, serial_data);
  link_on(CMD_ESP_UART, [](uint8_t addr, uint8_t data) { esp_serial.rx_queue_push(data); });
  link_on(CMD_RTC, [](uint8_t addr, uint8_t data) { zxrtc.setData(addr, data); });
  link_on(CMD_PS2_SCANCODE, ps2_command_receive);
  matrix_register_handlers();
  img_register_handlers();
  bk_register_handlers();
  printer_link_begin();

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
  Wire.setClock(400000); // all devices on the bus are fast-mode (SSD1306, HT16K33, DS3231, 24xx EEPROM, PCA9536)
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

  // also in MSC mode, and before it: then the host reads the card from the USBD task.
  // debug=0 (the default): the boot log so far is dropped, or the main loop would send it as
  // soon as a terminal opens the port
  load_setup();
  if (!hw_setup.debug_enabled) debug_discard();

  // msc mode asked by the reboot before (AT+KARABAS=msc, the msc button action)
  msc_check_boot();

  if (msc_active()) {
    // the boot core shows the msc screen; the card is read before the PC gets it
    osd_state = state_msc;
    if (has_sd && sd1.exists(FILENAME_BOOT)) do_configure(FILENAME_BOOT);
    msc_start();
  } else {
    osd_state = state_main;
    if (hw_setup.debug_enabled) hang_diag_start();

    // load boot from SD or flashfs
    if (has_sd && sd1.exists(FILENAME_BOOT)) {  
      do_configure(FILENAME_BOOT);
    } else {
      halt("Boot file not found. System stopped");
    }

    // the ESP8266 uart bridged by the boot core (Setup Wi-Fi, AT+KARABAS=esp)
    esp_serial.begin(spi_send);

    osd_state = state_core_browser;
    app_core_browser_read_list();
  }
}

// usb host init, runs on core1
static void usb_host_setup() {
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;
  pio_cfg.pinout = (PIO_USB_PINOUT) board_usb_pinout();

  // PIO-USB claims its default state machines (pio0 sm0, pio1 sm0/sm1) and pio1 memory:
  // release what usb_pio_reserve held for it since before main()
  pio_sm_unclaim(pio0, 0);
  for (uint i=0; i<4; i++) pio_sm_unclaim(pio1, i);

  // PIO-USB claims a hardware spinlock for its alarm pool and panics if none is left
  // (only 8 can be claimed, see usb_host_lock())
  int free_locks[8], n_free = 0;
  while (n_free < 8 && (free_locks[n_free] = spin_lock_claim_unused(false)) >= 0) n_free++;
  for (int i = 0; i < n_free; i++) spin_lock_unclaim(free_locks[i]);
  d_printf("USB host: %d hardware spinlocks free\r\n", n_free);

  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
  tuh_init(1);
  if (!pio_usb_host_programs_loaded()) {
    // pio_add_program failed: no PIO instruction memory left for the usb host programs
    d_println("USB host: PIO programs did not load, usb devices will not work");
  }
  usb_host_ready = true;
}

// firmware main loop, runs on core0
static void karabas_loop()
{
  // hang_mark: where APP is, for the hang diagnostics (tick << 8 | step)
  hang_mark(0x0100);
  diag_loop_core0();
  diag_tick();
  hang_mark(0x0200);
  debug_drain();

  //rp2040.wdt_reset();
  // the clock runs in msc mode too (the msc screen shows it)
  hang_mark(0x0300);
  zxrtc.handle();

  // in msc mode nothing else runs until eject
  hang_mark(0x0400);
  if (msc_tick()) return;

  // osd hide / popup timers
  hang_mark(0x0500);
  ui_tick();

  if (led_timer.elapsed() >= 100) {
      led_write(0, true);
      led_timer.reset();
  }

  hang_mark(0x0600);
  input_tick_buttons();
  hang_mark(0x0700);
  input_tick_ui();
  hang_mark(0x0800);
  input_tick_repeat();

  hang_mark(0x0900);
  osd_handle(false);

  hang_mark(0x0a00);
  link_report_drops();
  link_dispatch_pending();
  hang_mark(0x0b00);
  printer_link_tick();

  // sector requests of the file mounter
  hang_mark(0x0c00);
  img_tick();
  // a backup RAM file the core asked for
  bk_tick();

  hang_mark(0x0d00);
  serial_bridge_tick();

  // send byte from esp tx fifo
  hang_mark(0x0e00);
  esp_serial.handle();

  // let the fpga talk even when nothing else was sent
  hang_mark(0x0f00);
  link_poll();
}

// usb host loop, runs on core1
static void usb_host_loop()
{
  diag_loop_core1();
  hang_beat_core1();
  tuh_task();
  printer_host_tick(millis());
  printer_usb_tick();
}

// The firmware and the usb host run in their own pinned tasks.
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
  // incoming fpga commands are handled here, also when another task received them
  link_set_dispatch_task(xTaskGetCurrentTaskHandle());
  // and only this task writes to the usb serial port
  debug_set_owner_task(xTaskGetCurrentTaskHandle());
  // let PIO-USB claim its PIO memory, state machines and DMA channel first:
  // the SD card driver (PioSPI) would otherwise take PIO memory that the usb rx programs need
  ElapsedTimer wait;
  while (!usb_host_ready && wait.elapsed() < 2000) { delay(1); }
  karabas_setup();
  // usb / ps2 / joystick events are forwarded by their own task from now on
  input_task_start();
  // LED matrix and OLED too
  periph_task_start();
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
  // first: until it exists, the USB irq runs the device stack itself
  usb_device_task_start();
  link_os_init();
  // before the USB task: its hid callbacks push events into the link queue
  link_queue_init();
  i2c_bus_init();
  // tasks must be created already pinned: the USB task has a higher priority than the
  // framework's CORE0 task running this code, so it starts immediately on creation.
  // Unpinned, it could start on core0 and starve CORE0 forever (it never blocks),
  // and PIO-USB would set up its SOF timer on the wrong core.
  if (xTaskCreateAffinitySet(usb_task, "USB", USB_TASK_STACK, nullptr, USB_TASK_PRIORITY, 1 << 1, nullptr) != pdPASS) {
    panic("Unable to create USB task");
  }
  if (xTaskCreateAffinitySet(app_task, "APP", APP_TASK_STACK, nullptr, APP_TASK_PRIORITY, 1 << 0, nullptr) != pdPASS) {
    panic("Unable to create APP task");
  }
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}

void halt(const char* msg) {
  hang_diag_halt(msg);
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
