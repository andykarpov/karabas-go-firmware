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
#include "usb/hid_driver.h"
#include "tusb.h"
#include "pio_usb.h"
#include "input/ps2kbd.h"
#include "apps/app_core_browser.h"
#include "diag/diag.h"
#include "diag/hang_diag.h"
#include "diag/usb_diag.h"
#include "usb/printer_host.h"
#include "periph/printer_usb.h"
#include "periph/printer_link.h"

#include "usb/usb_device.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include <FreeRTOS.h>
#include <task.h>
// see FreeRTOSConfig.h: wakeups from irq context go through the timer task
_Static_assert(configTIMER_TASK_PRIORITY == configMAX_PRIORITIES - 1, "FreeRTOS timer task must run above APP and USB");

volatile bool usb_host_ready = false;

static uint32_t led_timer = 0;

#define USB_HOST_PIO pio1

// The usb host takes all of pio1: tx, rx and eop (5 + 10 + 17 = all 32 instruction words,
// sm0..2). The firmware's own programs (the PIO SPI of the SD card, fpga_serial) go to pio0.
// main() reserves the state machines of pio1, so nothing but the usb host can load a program
// there; usb_host_setup() releases them right before PIO-USB init.

// fpga commands handled here (link_on)
// a handler runs inside any spi_send of APP (a file transfer, an osd redraw): the core reload
// waits for the main loop, so the sequence it interrupted does not go on into the new core
static bool flashboot_pending = false;
static uint8_t flashboot_data;
static void on_flashboot(uint8_t addr, uint8_t data) {
  flashboot_data = data;
  flashboot_pending = true;
}

static void on_esp_uart(uint8_t addr, uint8_t data) {
  esp_serial_rx_push(data);
}

static void esp_send(uint8_t c) {
  spi_send(CMD_ESP_UART, 0, c);
}

static void on_rtc(uint8_t addr, uint8_t data) {
  rtc_set_data(addr, data);
}

// firmware init, runs on core0
static void karabas_setup()
{
  // the boot log goes into the buffer until karabas.ini says whether debug is on
  hw_setup.debug_enabled = true;
  hang_diag_report();

  //rp2040.wdt_begin(5000);

  joyL = joyR = 0;

  memset(file_slots, 0, sizeof(file_slots));

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
  // all devices on the bus are fast-mode (SSD1306, HT16K33, DS3231, 24xx EEPROM, PCA9536),
  // the board sets the clock its bus is good for
  i2c_dev_begin(PIN_I2C_SDA, PIN_I2C_SCL, board_i2c_hz());

#if WAIT_SERIAL
  while (!tud_cdc_connected()) ms_delay(10); // wait for a terminal
#endif

  serial_bridge_begin();
  d_println("Karabas Go RP2040 firmware");

  // buttons, leds, joysticks
  board_init_io();

  matrix_begin();
  oled_begin();

  rtc_begin(spi_send, on_time);
  osd_begin(spi_send);
  ft_begin(spi_send, PIN_MCU_FT_CS, board_ft_reset_pin());

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
    if (has_sd && sd_exists(FILENAME_BOOT)) do_configure(FILENAME_BOOT);
    msc_start();
  } else {
    osd_state = state_main;
    if (hw_setup.debug_enabled) hang_diag_start();

    // load boot from SD or flashfs
    if (has_sd && sd_exists(FILENAME_BOOT)) {  
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

  // release what main() held for the usb host
  for (uint i=0; i<4; i++) pio_sm_unclaim(USB_HOST_PIO, i);

  // PIO-USB claims a hardware spinlock for its alarm pool and panics if none is left
  // (only 8 can be claimed, see usb_host_lock())
  int free_locks[8], n_free = 0;
  while (n_free < 8 && (free_locks[n_free] = spin_lock_claim_unused(false)) >= 0) n_free++;
  for (int i = 0; i < n_free; i++) spin_lock_unclaim(free_locks[i]);
  d_printf("USB host: %d hardware spinlocks free\r\n", n_free);

  tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
  tuh_init(BOARD_TUH_RHPORT);
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
  rtc_handle();

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
  if (flashboot_pending) {
    flashboot_pending = false;
    flashboot(flashboot_data);
  }
  hang_mark(0x0b00);
  printer_link_tick();

  // sector requests of the file mounter
  hang_mark(0x0c00);
  img_tick();
  // a backup RAM file the core asked for
  bk_tick();
  core_eeprom_tick();

  hang_mark(0x0d00);
  serial_bridge_tick();

  // send a byte from the esp tx ring
  hang_mark(0x0e00);
  esp_serial_handle();

  // let the fpga talk even when nothing else was sent
  hang_mark(0x0f00);
  link_poll();
}

// usb host loop, runs on core1. It never blocks, so all of it runs from RAM: from flash,
// each turn of it took code of APP out of the 16 KB flash cache that both cores share,
// and the misses made core0 2-3 times slower (phase 1.6, the text OSD drew visibly).
static void __not_in_flash_func(usb_host_loop)()
{
  diag_loop_core1();
  hang_beat_core1();
  usb_diag_tick();
  // timeout 0: on OPT_OS_FREERTOS tuh_task() would block on its queue after the last event
  if (tuh_task_event_ready()) tuh_task_ext(0, false);
  printer_host_tick();
  printer_usb_tick();
}

// The firmware and the usb host run in their own pinned tasks, created by main().

#ifndef APP_TASK_STACK
#define APP_TASK_STACK 4096 // words
#endif
#ifndef USB_TASK_STACK
#define USB_TASK_STACK 2048 // words
#endif
#define APP_TASK_PRIORITY (configMAX_PRIORITIES / 2)
#define USB_TASK_PRIORITY (configMAX_PRIORITIES - 2) // above everything but the timer task

static void app_task(void *param) {
  // incoming fpga commands are handled here, also when another task received them
  link_set_dispatch_task(xTaskGetCurrentTaskHandle());
  // and only this task writes to the usb serial port
  debug_set_owner_task(xTaskGetCurrentTaskHandle());
  // let PIO-USB claim its PIO memory, state machines and DMA channel first:
  // the SD card driver (lib/sd sd_pio) would otherwise take PIO memory that the usb programs need (all of pio1)
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

static void __not_in_flash_func(usb_task)(void *param) {
  usb_host_setup();
  for (;;) {
    usb_host_loop();
  }
}

// FreeRTOS hooks: the message is kept for the next boot's log (hang_diag, --wrap=panic)
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
  (void) task;
  // a literal per task: only the pointer to the message is kept over the reboot
  static const char *const msgs[][2] = {
    { "APP", "stack overflow: APP" }, { "USB", "stack overflow: USB" }, { "USBD", "stack overflow: USBD" },
    { "INPUT", "stack overflow: INPUT" }, { "MATRIX", "stack overflow: MATRIX" }, { "OLED", "stack overflow: OLED" },
    { "WDOG", "stack overflow: WDOG" }, { "Tmr Svc", "stack overflow: Tmr Svc" }, { "IDLE0", "stack overflow: IDLE0" },
    { "IDLE1", "stack overflow: IDLE1" },
  };
  for (size_t i = 0; i < sizeof(msgs) / sizeof(msgs[0]); i++) {
    if (strcmp(name, msgs[i][0]) == 0) panic(msgs[i][1]);
  }
  panic("stack overflow");
}

void vApplicationMallocFailedHook(void) {
  panic("malloc failed");
}

int main() {
  // PIO-USB needs a multiple of 12 MHz; clk_peri (SPI, I2C) stays at 48 MHz from the USB PLL
  set_sys_clock_khz(120000, true);
  pio_claim_sm_mask(USB_HOST_PIO, 0xf);
  // the USB name of the card (msc / msc2), asked by the PC as soon as USB comes up
  msc_init();
  usb_device_task_start();
  link_os_init();
  // before the USB task: its hid callbacks push events into the link queue
  link_queue_init();
  i2c_bus_init();
  // pinned at creation: PIO-USB sets up its SOF timer on the core that starts it
  if (xTaskCreateAffinitySet(usb_task, "USB", USB_TASK_STACK, NULL, USB_TASK_PRIORITY, 1 << 1, NULL) != pdPASS) {
    panic("Unable to create USB task");
  }
  if (xTaskCreateAffinitySet(app_task, "APP", APP_TASK_STACK, NULL, APP_TASK_PRIORITY, 1 << 0, NULL) != pdPASS) {
    panic("Unable to create APP task");
  }
  vTaskStartScheduler();
  return 0;
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
  chip_reboot();
}
