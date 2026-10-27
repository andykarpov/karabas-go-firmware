// Karabas Go pico-sdk spike (docs/rewrite_plan.md, phase 0): the firmware's USB setup on the plain
// pico-sdk, without arduino-pico, Adafruit TinyUSB and the patches.
//
// - FreeRTOS SMP with the pico_sync interop; TinyUSB on OPT_OS_FREERTOS.
// - USBD (core0): tud_task(), blocking on TinyUSB's queue. CDC (the log) + MSC (a RAM disk).
// - USB (core1): tuh_task() on PIO-USB (lib/Pico-PIO-USB), HID devices behind the hub.
// - APP (core0): never blocks, like the firmware's main loop: drains the log into the CDC, a DIAG
//   line every 5 s, the LED. Its CDC writes run next to tud_task() with no __usb_mutex.
//
// Keys on the terminal: f = flood the CDC (the old core0 hang case), s = DIAG now,
// b = BOOTSEL. A 1200 baud touch reboots into BOOTSEL too (pio upload).

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/critical_section.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/structs/iobank0.h"

#include "FreeRTOS.h"
#include "task.h"

#include "tusb.h"
#include "pio_usb.h"

#if HW_ID == 2 || HW_ID == 3 // Mini, Mini G
#define HOST_PIN_DP   24
#define HOST_PINOUT   PIO_USB_PINOUT_DPDM
#define PIN_BTN1      4
#define PIN_BTN2      6
#define PIN_LED1      7
#define PIN_LED2      5
#else
#error "the spike knows only the Mini boards (HW_ID 2, 3)"
#endif
#if HW_ID == 3
#define PIN_FT_RESET   27
#define PIN_MIDI_RESET 28
#define PIN_ESP_RESET  29
#endif

#define APP_PRIORITY  (configMAX_PRIORITIES / 2)         // 4
#define USBD_PRIORITY (configMAX_PRIORITIES / 2 + 1)     // 5: enumeration while APP runs
#define USB_PRIORITY  (configMAX_PRIORITIES - 2)         // 6, below the timer task
#define DIAG_EVERY_MS 5000

static TaskHandle_t app_task_handle, usb_task_handle, usbd_task_handle;

//--------------------------------------------------------------------+
// Log: from any task, core or irq into a buffer; APP sends it to the CDC once a terminal opens
// the port (the boot log waits for it, the rest is dropped when the buffer is full)
//--------------------------------------------------------------------+

#define LOG_BUF_SIZE 8192
static uint8_t log_buf[LOG_BUF_SIZE];
static uint32_t log_head, log_tail, log_dropped;
static critical_section_t log_cs;

static void log_write(const char *s, size_t len) {
  critical_section_enter_blocking(&log_cs);
  for (size_t i = 0; i < len; i++) {
    if (log_head - log_tail >= LOG_BUF_SIZE) {
      log_dropped += len - i;
      break;
    }
    log_buf[log_head++ % LOG_BUF_SIZE] = (uint8_t) s[i];
  }
  critical_section_exit(&log_cs);
}

static void log_printf(const char *fmt, ...) {
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof(line) - 2, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > (int) sizeof(line) - 3) n = sizeof(line) - 3;
  line[n++] = '\r';
  line[n++] = '\n';
  log_write(line, (size_t) n);
}

static uint32_t log_free(void) {
  critical_section_enter_blocking(&log_cs);
  uint32_t used = log_head - log_tail;
  critical_section_exit(&log_cs);
  return LOG_BUF_SIZE - used;
}

// APP only: as much as the CDC fifo takes now, never waits
static void log_drain(void) {
  if (!tud_cdc_connected()) return;
  for (;;) {
    uint32_t room = tud_cdc_write_available();
    if (room == 0) break;
    critical_section_enter_blocking(&log_cs);
    uint32_t used = log_head - log_tail;
    uint32_t pos = log_tail % LOG_BUF_SIZE;
    uint32_t n = used < LOG_BUF_SIZE - pos ? used : LOG_BUF_SIZE - pos;
    critical_section_exit(&log_cs);
    if (n == 0) break;
    if (n > room) n = room;
    n = tud_cdc_write(&log_buf[pos], n);
    critical_section_enter_blocking(&log_cs);
    log_tail += n;
    critical_section_exit(&log_cs);
    if (n == 0) break;
  }
  tud_cdc_write_flush();
}

//--------------------------------------------------------------------+
// The reason of the last reboot: kept in RAM that a watchdog reboot does not clear
//--------------------------------------------------------------------+

#define CRASH_MAGIC 0x4b47534bu
static uint32_t __uninitialized_ram(crash_magic);
static char __uninitialized_ram(crash_text)[96];

static void __attribute__((noreturn)) crash(const char *fmt, va_list ap) {
  vsnprintf(crash_text, sizeof(crash_text), fmt, ap);
  crash_magic = CRASH_MAGIC;
  watchdog_reboot(0, 0, 10);
  for (;;) tight_loop_contents();
}

// PICO_PANIC_FUNCTION: TinyUSB, the SDK, PIO-USB
void __attribute__((noreturn)) spike_panic(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  crash(fmt, ap);
}

static void __attribute__((noreturn)) crashf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  crash(fmt, ap);
}

void spike_assert(const char *file, int line) {
  crashf("assert %s:%d", file, line);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
  (void) task;
  crashf("stack overflow in %s", name);
}

void vApplicationMallocFailedHook(void) {
  crashf("malloc failed");
}

//--------------------------------------------------------------------+
// USB host (USB task, core1)
//--------------------------------------------------------------------+

static volatile uint32_t hid_reports, host_wakeups;
static volatile uint8_t devices_mounted;

void tuh_mount_cb(uint8_t daddr) {
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(daddr, &vid, &pid);
  devices_mounted++;
  log_printf("USB device %u mounted: %04x:%04x", daddr, vid, pid);
}

void tuh_umount_cb(uint8_t daddr) {
  if (devices_mounted) devices_mounted--;
  log_printf("USB device %u unmounted", daddr);
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t instance, const uint8_t *desc, uint16_t desc_len) {
  (void) desc;
  static const char *const protocols[] = {"none", "keyboard", "mouse"};
  uint8_t proto = tuh_hid_interface_protocol(daddr, instance);
  log_printf("HID %u.%u mounted: %s, report descriptor %u bytes", daddr, instance,
             proto < 3 ? protocols[proto] : "?", desc_len);
  if (!tuh_hid_receive_report(daddr, instance)) {
    log_printf("HID %u.%u: unable to request a report", daddr, instance);
  }
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t instance) {
  log_printf("HID %u.%u unmounted", daddr, instance);
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t instance, const uint8_t *report, uint16_t len) {
  hid_reports++;
  // boot keyboards: the pressed keys, on a change
  if (tuh_hid_interface_protocol(daddr, instance) == HID_ITF_PROTOCOL_KEYBOARD && len >= 8) {
    static uint8_t last[8];
    if (memcmp(last, report, 8) != 0) {
      memcpy(last, report, 8);
      log_printf("key %u.%u: mod %02x keys %02x %02x %02x %02x %02x %02x", daddr, instance,
                 report[0], report[2], report[3], report[4], report[5], report[6], report[7]);
    }
  }
  tuh_hid_receive_report(daddr, instance);
}

static void usb_task(void *param) {
  (void) param;
  // PIO-USB claims a hardware spinlock for its alarm pool and panics if none is left
  int locks[8], n_free = 0;
  while (n_free < 8 && (locks[n_free] = spin_lock_claim_unused(false)) >= 0) n_free++;
  for (int i = 0; i < n_free; i++) spin_lock_unclaim(locks[i]);

  pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
  cfg.pin_dp = HOST_PIN_DP;
  cfg.pinout = HOST_PINOUT;
#ifdef SPIKE_USB_PIO
  // upstream 0.7.x: tx, rx and eop on one PIO block (all 32 words), the other one stays free
  cfg.pio_tx_num = SPIKE_USB_PIO;
  cfg.pio_rx_num = SPIKE_USB_PIO;
  cfg.sm_tx = 0;
  cfg.sm_rx = 1;
  cfg.sm_eop = 2;
#endif
  tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &cfg);
  // here, on core1: PIO-USB runs its SOF timer on the core that starts it
  tusb_rhport_init_t host_init = { .role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO };
  tusb_init(BOARD_TUH_RHPORT, &host_init);
  log_printf("USB host: started, %d hardware spinlocks were free, PIO programs %s (tx pio%d, rx pio%d)",
             n_free, pio_usb_host_programs_loaded() ? "loaded" : "DID NOT LOAD", cfg.pio_tx_num,
             cfg.pio_rx_num);
  for (;;) {
    tuh_task(); // blocks until an event
    host_wakeups++;
  }
}

//--------------------------------------------------------------------+
// USB device (USBD task, core0)
//--------------------------------------------------------------------+

void tud_mount_cb(void) { log_printf("USB device port: configured"); }
void tud_umount_cb(void) { log_printf("USB device port: unconfigured"); }
void tud_suspend_cb(bool remote_wakeup_en) { (void) remote_wakeup_en; log_printf("USB device port: suspended"); }
void tud_resume_cb(void) { log_printf("USB device port: resumed"); }

// the 1200 baud touch of pio upload / arduino tools
void tud_cdc_line_coding_cb(uint8_t itf, const cdc_line_coding_t *coding) {
  (void) itf;
  if (coding->bit_rate == 1200) reset_usb_boot(0, 0);
}

static void usbd_task(void *param) {
  (void) param;
  // here, on core0: the USB irq goes to the core that starts the stack
  tusb_rhport_init_t dev_init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO };
  tusb_init(BOARD_TUD_RHPORT, &dev_init);
  for (;;) {
    tud_task(); // blocks until an event
  }
}

//--------------------------------------------------------------------+
// APP (core0)
//--------------------------------------------------------------------+

static const char *const line_names[4] = {"SE0", "FS idle", "LS idle", "SE1"};

// who drives the pin: "pio0 out 0" is our tx, "in" is nothing on our side (usb_diag.cpp)
static void pin_state(char *buf, size_t size, uint pin) {
  uint32_t st = io_bank0_hw->io[pin].status;
  bool oe = st & IO_BANK0_GPIO0_STATUS_OETOPAD_BITS;
  bool out = st & IO_BANK0_GPIO0_STATUS_OUTTOPAD_BITS;
  bool in = st & IO_BANK0_GPIO0_STATUS_INFROMPAD_BITS;
  uint func = gpio_get_function(pin);
  const char *fn = func == GPIO_FUNC_PIO0 ? "pio0" : func == GPIO_FUNC_PIO1 ? "pio1" :
                   func == GPIO_FUNC_SIO ? "sio" : "fn";
  snprintf(buf, size, "%s %s %d", fn, oe ? "out" : "in", oe ? out : in);
}

static uint32_t stack_free(TaskHandle_t t) {
  return t ? (uint32_t) uxTaskGetStackHighWaterMark(t) : 0;
}

static uint32_t app_loops, app_max_gap_us;

static void diag_print(void) {
  static uint32_t last_ms, last_frame, last_loops, last_reports, last_wakeups;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  uint32_t dt = last_ms ? now - last_ms : 0;
  uint32_t frame = pio_usb_host_get_frame_number();
  uint32_t per_s = dt ? 1000u / dt : 0;
  uint32_t sof = dt ? (uint32_t) ((uint64_t) (frame - last_frame) * 1000u / dt) : 0;
  uint32_t loops = dt ? (uint32_t) ((uint64_t) (app_loops - last_loops) * 1000u / dt) : 0;
  uint32_t reports = hid_reports, wakeups = host_wakeups;
  (void) per_s;
  uint8_t root = pio_usb_host_root_state();
  char dp[16], dm[16];
  pin_state(dp, sizeof(dp), HOST_PIN_DP);
  pin_state(dm, sizeof(dm), HOST_PIN_DP + 1);
  log_printf("DIAG %lu s | app %lu loops/s, max gap %lu us | heap free %u | stack free APP %lu USB %lu"
             " USBD %lu words | usb sof %lu/s, max gap %lu us, max frame %lu us, root %s%s%s line %s"
             " (D+ %s, D- %s), conn %lu disc %lu own %lu ovf %lu | devices %u, hid reports %lu,"
             " host wakeups %lu | log dropped %lu",
             (unsigned long) (now / 1000), (unsigned long) loops, (unsigned long) app_max_gap_us,
             (unsigned) xPortGetFreeHeapSize(), (unsigned long) stack_free(app_task_handle),
             (unsigned long) stack_free(usb_task_handle), (unsigned long) stack_free(usbd_task_handle),
             (unsigned long) sof, (unsigned long) pio_usb_diag.max_sof_gap_us,
             (unsigned long) pio_usb_diag.max_frame_us,
             (root & 1) ? "" : "uninit ", (root & 2) ? "connected" : "empty",
             (root & 4) ? " suspended" : "", line_names[(root >> 4) & 3], dp, dm,
             (unsigned long) pio_usb_diag.connects, (unsigned long) pio_usb_diag.disconnects,
             (unsigned long) pio_usb_diag.own_se0, (unsigned long) pio_usb_diag.rx_overflows,
             devices_mounted, (unsigned long) (reports - last_reports),
             (unsigned long) (wakeups - last_wakeups), (unsigned long) log_dropped);
  pio_usb_diag.max_sof_gap_us = 0;
  pio_usb_diag.max_frame_us = 0;
  app_max_gap_us = 0;
  last_ms = now; last_frame = frame; last_loops = app_loops;
  last_reports = reports; last_wakeups = wakeups;
}

// a root port disconnect, as the SOF irq counted it, with the pins right after
static void root_events(void) {
  static uint32_t disconnects;
  uint32_t d = pio_usb_diag.disconnects;
  if (d == disconnects) return;
  disconnects = d;
  uint8_t root = pio_usb_host_root_state();
  char dp[16], dm[16];
  pin_state(dp, sizeof(dp), HOST_PIN_DP);
  pin_state(dm, sizeof(dm), HOST_PIN_DP + 1);
  log_printf("USB root: disconnect (%lu in all, %lu own), line %s, D+ %s, D- %s", (unsigned long) d,
             (unsigned long) pio_usb_diag.own_se0, line_names[(root >> 4) & 3], dp, dm);
}

static void app_task(void *param) {
  (void) param;
  if (crash_magic == CRASH_MAGIC) {
    crash_text[sizeof(crash_text) - 1] = 0;
    log_printf("Last boot crashed: %s", crash_text);
  }
  crash_magic = 0;
  log_printf("Karabas Go pico-sdk spike, HW_ID %d, %lu MHz, FreeRTOS %s, TinyUSB %d.%d.%d", HW_ID,
             (unsigned long) (clock_get_hz(clk_sys) / 1000000), tskKERNEL_VERSION_NUMBER,
             TUSB_VERSION_MAJOR, TUSB_VERSION_MINOR, TUSB_VERSION_REVISION);

  bool flood = false, btn_last = false, led = false;
  uint32_t flood_n = 0;
  uint64_t last_us = time_us_64();
  absolute_time_t next_diag = make_timeout_time_ms(DIAG_EVERY_MS);
  absolute_time_t next_led = make_timeout_time_ms(250);
  // no blocking and no yield, like the firmware's main loop: USBD preempts it
  for (;;) {
    uint64_t now_us = time_us_64();
    if (now_us - last_us > app_max_gap_us) app_max_gap_us = (uint32_t) (now_us - last_us);
    last_us = now_us;
    app_loops++;

    root_events();

    if (tud_cdc_available()) {
      char c;
      if (tud_cdc_read(&c, 1) == 1) {
        if (c == 'f') { flood = !flood; log_printf("flood %s", flood ? "on" : "off"); }
        if (c == 's') diag_print();
        if (c == 'b') reset_usb_boot(0, 0);
      }
    }
    bool btn = !gpio_get(PIN_BTN1);
    if (btn && !btn_last) { flood = !flood; log_printf("flood %s", flood ? "on" : "off"); }
    btn_last = btn;
    if (flood && log_free() > 1024) {
      log_printf("flood %lu: the quick brown fox jumps over the lazy dog 0123456789 abcdefghijklmnopqrstuvwxyz",
                 (unsigned long) flood_n++);
    }

    if (time_reached(next_diag)) {
      next_diag = make_timeout_time_ms(DIAG_EVERY_MS);
      diag_print();
    }
    if (time_reached(next_led)) {
      next_led = make_timeout_time_ms(250);
      led = !led;
      gpio_put(PIN_LED1, !led);
    }

    log_drain();
  }
}

//--------------------------------------------------------------------+

static void board_init(void) {
  gpio_init(PIN_LED1); gpio_set_dir(PIN_LED1, GPIO_OUT); gpio_put(PIN_LED1, 1);
  gpio_init(PIN_LED2); gpio_set_dir(PIN_LED2, GPIO_OUT); gpio_put(PIN_LED2, 1);
  gpio_init(PIN_BTN1); gpio_pull_up(PIN_BTN1);
  gpio_init(PIN_BTN2); gpio_pull_up(PIN_BTN2);
#if HW_ID == 3
  const uint resets[] = {PIN_FT_RESET, PIN_MIDI_RESET, PIN_ESP_RESET};
  for (unsigned i = 0; i < 3; i++) {
    gpio_init(resets[i]); gpio_set_dir(resets[i], GPIO_OUT); gpio_put(resets[i], 1);
  }
#endif
}

int main(void) {
  // PIO-USB needs a multiple of 12 MHz (as the firmware's f_cpu)
  set_sys_clock_khz(120000, true);
  critical_section_init(&log_cs);
  board_init();

  // pinned at creation, as in the firmware
  xTaskCreateAffinitySet(usbd_task, "USBD", 1024, NULL, USBD_PRIORITY, 1 << 0, &usbd_task_handle);
  xTaskCreateAffinitySet(usb_task, "USB", 2048, NULL, USB_PRIORITY, 1 << 1, &usb_task_handle);
  xTaskCreateAffinitySet(app_task, "APP", 2048, NULL, APP_PRIORITY, 1 << 0, &app_task_handle);
  vTaskStartScheduler();
  return 0;
}
