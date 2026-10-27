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
#include "diag/usb_diag.h"
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

static uint32_t led_timer = 0;

#define USB_HOST_PIO pio1

// The usb host takes all of pio1: tx, rx and eop (5 + 10 + 17 = all 32 instruction words,
// sm0..2). The firmware's own programs (ccount, PioSPI of the SD card, fpga_serial) go to pio0.
// Under FreeRTOS SysTick belongs to the scheduler, so the core runs its cycle counter (ccount,
// 2 instructions) on a PIO state machine, allocated in main() before setup(); on pio1 it would
// leave no room for the usb programs and the host would fail silently. Reserve the state
// machines of pio1 in a static constructor, which runs before main(), so nothing but the usb
// host can load a program there. usb_host_setup() releases them right before PIO-USB init.
static struct UsbPioReserve {
  UsbPioReserve() {
    pio_claim_sm_mask(USB_HOST_PIO, 0xf);
  }
} usb_pio_reserve;

// fpga commands handled here (link_on)
static void on_flashboot(uint8_t addr, uint8_t data) {
  flashboot(data);
}

static void on_esp_uart(uint8_t addr, uint8_t data) {
  esp_serial_rx_push(data);
}

static void esp_send(uint8_t c) {
  spi_send(CMD_ESP_UART, 0, c);
}

static void on_rtc(uint8_t addr, uint8_t data) {
  zxrtc.setData(addr, data);
}

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
  link_on(CMD_FLASHBOOT, on_flashboot);
  link_on(CMD_UART, serial_data);
  link_on(CMD_ESP_UART, on_esp_uart);
  link_on(CMD_RTC, on_rtc);
  link_on(CMD_PS2_SCANCODE, ps2_command_receive);
  matrix_register_handlers();
  img_register_handlers();
  bk_register_handlers();
  printer_link_begin();

  // FPGA bitstream loader
  pin_input_pullup(PIN_CONF_INIT_B);
  pin_output(PIN_CONF_PRG_B);
  pin_input(PIN_CONF_DONE);
  pin_input(PIN_CONF_CLK);
  pin_input(PIN_CONF_IO1);

  // FT, SD2, MCU CS lines 
  pin_output(PIN_MCU_SPI_CS); gpio_put(PIN_MCU_SPI_CS, 1);
  pin_output(PIN_MCU_SD2_CS); gpio_put(PIN_MCU_SD2_CS, 1);
  pin_output(PIN_MCU_FT_CS); gpio_put(PIN_MCU_FT_CS, 1);

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
    esp_serial_begin(esp_send);

    osd_state = state_core_browser;
    app_core_browser_read_list();
  }
}

// usb host init, runs on core1
static void usb_host_setup() {
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;
  pio_cfg.pinout = (PIO_USB_PINOUT) board_usb_pinout();
  pio_cfg.pio_tx_num = PIO_NUM(USB_HOST_PIO);
  pio_cfg.pio_rx_num = PIO_NUM(USB_HOST_PIO);
  pio_cfg.sm_tx = 0;
  pio_cfg.sm_rx = 1;
  pio_cfg.sm_eop = 2;

  // release what usb_pio_reserve held for the usb host since before main()
  for (uint i=0; i<4; i++) pio_sm_unclaim(USB_HOST_PIO, i);

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

  if (ms_since(led_timer) >= 100) {
      led_write(0, true);
      led_timer = ms_now();
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

  // send a byte from the esp tx ring
  hang_mark(0x0e00);
  esp_serial_handle();

  // let the fpga talk even when nothing else was sent
  hang_mark(0x0f00);
  link_poll();
}

// usb host loop, runs on core1
static void usb_host_loop()
{
  diag_loop_core1();
  hang_beat_core1();
  usb_diag_tick();
  tuh_task();
  printer_host_tick(ms_now());
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
  // the SD card driver (PioSPI) would otherwise take PIO memory that the usb programs need (all of pio1)
  uint32_t wait = ms_now();
  while (!usb_host_ready && ms_since(wait) < 2000) { ms_delay(1); }
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
      ms_delay(100);
  }
  rp2040.reboot();
}
