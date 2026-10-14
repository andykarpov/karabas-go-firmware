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

#ifdef __FREERTOS
// Under FreeRTOS the core puts its cycle counter (ccount) on the first free PIO state machine
// in main(), before setup(). Reserve pio0 sm0 for the PIO-USB transmitter already in a static
// constructor (runs before main), so the usb host gets the same PIO layout as without FreeRTOS.
// usb_host_setup() releases it right before PIO-USB claims it.
static struct UsbPioReserve {
  UsbPioReserve() { pio_sm_claim(pio0, 0); }
} usb_pio_reserve;
#endif

// RTOS_PROBE (env *_rtos_probe): boot step by step and report every step to USB serial,
// to find where the RTOS build stops on real hardware.
// 1 = full boot, 2 = usb host task only (core0 keeps reporting if core1 hangs)
#ifndef RTOS_PROBE
#define RTOS_PROBE 0
#endif

#if defined(__FREERTOS) && RTOS_PROBE
static volatile uint32_t probe_app_loops = 0;
static volatile uint32_t probe_usb_loops = 0;

static void probe(const char *msg) {
  Serial.print("PROBE: "); Serial.println(msg);
  Serial.flush();
  delay(200); // let the line leave before a possible crash
}
#define PROBE(msg) probe(msg)
#define PROBE_COUNT(var) (var)++

// called from lib/Pico-PIO-USB (weak reference there)
extern "C" void pio_usb_probe(const char *msg) { probe(msg); }
#else
#define PROBE(msg)
#define PROBE_COUNT(var)
#endif

// PIO_USB_PROBE_COUNTERS (env *_probe*): usb host bus counters, printed every 2 s,
// works with and without FreeRTOS to compare the two builds
#ifdef PIO_USB_PROBE_COUNTERS
extern "C" void pio_usb_probe_state(uint32_t *sof_count, int *connected, int *line_state, int *alarm_core, int *alarm_num);
extern "C" void pio_usb_probe_port(uint32_t out[6]);
extern "C" volatile uint32_t pio_usb_probe_xfer[6];
extern "C" volatile int32_t pio_usb_probe_hs[4];
static volatile int probe_sof_irq_enabled_core1 = -1;

// runs on core1 after usb init: NVIC is per core
static void usb_probe_record_irq() {
  uint32_t sof; int conn, line, acore, anum;
  pio_usb_probe_state(&sof, &conn, &line, &acore, &anum);
  if (anum >= 0) probe_sof_irq_enabled_core1 = irq_is_enabled(TIMER_IRQ_0 + anum);
}

static uint8_t pio_claimed_mask(PIO pio) {
  uint8_t m = 0;
  for (uint8_t i=0; i<4; i++) if (pio_sm_is_claimed(pio, i)) m |= (1 << i);
  return m;
}

static void usb_probe_print() {
  uint32_t sof; int conn, line, acore, anum;
  pio_usb_probe_state(&sof, &conn, &line, &acore, &anum);
  Serial.printf("PROBE: usb sof frames %lu, root connected %d, line state %d, alarm core %d num %d, irq enabled on core1 %d, timer inte %02lx armed %02lx\r\n",
    (unsigned long) sof, conn, line, acore, anum, probe_sof_irq_enabled_core1,
    (unsigned long) timer_hw->inte, (unsigned long) timer_hw->armed);
  uint32_t port[6];
  pio_usb_probe_port(port);
  Serial.printf("PROBE: pio0 claimed %x enabled %lx, pio1 claimed %x enabled %lx | usb sm tx %lu rx %lu eop %lu, offset tx %lu rx %lu eop %lu\r\n",
    pio_claimed_mask(pio0), (unsigned long) (pio0->ctrl & 0xf), pio_claimed_mask(pio1), (unsigned long) (pio1->ctrl & 0xf),
    (unsigned long) port[0], (unsigned long) port[1], (unsigned long) port[2],
    (unsigned long) port[3], (unsigned long) port[4], (unsigned long) port[5]);
  Serial.printf("PROBE: usb xfers: setup %lu in %lu out %lu | complete %lu error %lu stalled %lu | last handshake: loops left %ld, bytes %02lx %02lx, rx fifo %ld\r\n",
    (unsigned long) pio_usb_probe_xfer[0], (unsigned long) pio_usb_probe_xfer[1], (unsigned long) pio_usb_probe_xfer[2],
    (unsigned long) pio_usb_probe_xfer[3], (unsigned long) pio_usb_probe_xfer[4], (unsigned long) pio_usb_probe_xfer[5],
    (long) pio_usb_probe_hs[0], (unsigned long) pio_usb_probe_hs[1], (unsigned long) pio_usb_probe_hs[2], (long) pio_usb_probe_hs[3]);
}
#endif


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

  // PIO-USB hard-claims its state machines (default pio0 sm0, pio1 sm0/sm1) and panics if
  // one is taken. Under FreeRTOS the core already uses a PIO SM (usually pio0 sm0) as cycle
  // counter, so pick free ones. Without FreeRTOS this yields the same defaults.
#ifdef __FREERTOS
  pio_sm_unclaim(pio0, 0); // reserved before main(), see usb_pio_reserve
#endif
  int sm_tx = pio_claim_unused_sm(pio0, false);
  int sm_rx = pio_claim_unused_sm(pio1, false);
  int sm_eop = pio_claim_unused_sm(pio1, false);
  if (sm_tx >= 0) { pio_sm_unclaim(pio0, sm_tx); pio_cfg.sm_tx = sm_tx; }
  if (sm_rx >= 0) { pio_sm_unclaim(pio1, sm_rx); pio_cfg.sm_rx = sm_rx; }
  if (sm_eop >= 0) { pio_sm_unclaim(pio1, sm_eop); pio_cfg.sm_eop = sm_eop; }

  PROBE("tuh_configure");
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
  PROBE("tuh_init");
  tuh_init(1);
  PROBE("tuh_init returned");
#ifdef PIO_USB_PROBE_COUNTERS
  usb_probe_record_irq();
#endif
  usb_host_ready = true;
}

// firmware main loop, runs on core0
static void karabas_loop()
{
  diag_loop_core0();
  diag_tick();
#if defined(PIO_USB_PROBE_COUNTERS) && !defined(__FREERTOS)
  static ElapsedTimer probe_timer;
  if (probe_timer.elapsed() >= 2000) { probe_timer.reset(); usb_probe_print(); }
#endif

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
  PROBE("APP task started");
  // let PIO-USB claim its PIO memory, state machines and DMA channel first:
  // the SD card driver (PioSPI) would otherwise take PIO memory that the usb rx programs need
  ElapsedTimer wait;
  while (!usb_host_ready && wait.elapsed() < 2000) { delay(1); }
  PROBE(usb_host_ready ? "APP: usb host is ready" : "APP: usb host not ready, continuing");
  karabas_setup();
  PROBE("karabas_setup done");
  // no yield here: every scheduler call takes the SMP kernel lock with interrupts off,
  // which could delay the PIO-USB SOF interrupt on core1
  for (;;) {
    karabas_loop();
    PROBE_COUNT(probe_app_loops);
  }
}

static void usb_task(void *param) {
  PROBE("USB task started");
  usb_host_setup();
  PROBE("usb_host_setup done");
  for (;;) {
    usb_host_loop();
    PROBE_COUNT(probe_usb_loops);
  }
}

void setup() {
#if RTOS_PROBE
#ifdef PIN_LED1
  // LED1 on: the scheduler runs and the framework's CORE0 task reached setup()
  pinMode(PIN_LED1, OUTPUT); digitalWrite(PIN_LED1, LOW);
#endif
  Serial.begin(115200);
  // give the host time to enumerate the CDC port and open a terminal
  while (!Serial && millis() < 15000) { delay(10); }
  delay(500);
  PROBE("scheduler running, CORE0 task in setup()");
  PROBE("creating USB task");
  if (xTaskCreateAffinitySet(usb_task, "USB", USB_TASK_STACK, nullptr, USB_TASK_PRIORITY, 1 << 1, nullptr) != pdPASS) {
    PROBE("USB task creation FAILED");
  }
  delay(1000);
  PROBE(usb_host_ready ? "usb_host_ready = 1" : "usb_host_ready = 0");
#if RTOS_PROBE == 1
  PROBE("creating APP task");
  if (xTaskCreateAffinitySet(app_task, "APP", APP_TASK_STACK, nullptr, APP_TASK_PRIORITY, 1 << 0, nullptr) != pdPASS) {
    PROBE("APP task creation FAILED");
  }
#else
  PROBE("APP task skipped (RTOS_PROBE=2)");
#endif
  PROBE("setup() done");
  return;
#endif
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
#if RTOS_PROBE
  delay(2000);
  Serial.printf("PROBE: alive, app loops %lu, usb loops %lu, usb_host_ready %d, heap free %d\r\n",
    (unsigned long) probe_app_loops, (unsigned long) probe_usb_loops, usb_host_ready, rp2040.getFreeHeap());
  usb_probe_print();
#else
  vTaskDelay(portMAX_DELAY);
#endif
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
