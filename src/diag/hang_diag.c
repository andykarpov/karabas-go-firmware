#include "main.h"
#include "hang_diag.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "hardware/divider.h"

#include <FreeRTOS.h>
#include <task.h>

// scratch 1: magic << 24 | APP mark; after a panic: panic magic << 24 | exception << 16 | mark;
// after a halt(): halt magic << 24 | mark
// scratch 2: SPI holder, SPI waiter, I2C holder, I2C waiter (task tags, one byte each);
// after a panic or a halt(): its message (in flash)
// scratch 3: the second (since boot, mod 256) each one last ran: byte 0 the watchdog task,
// 1 APP, 2 the USB loop on core1, 3 a timer irq on core0. Each byte is written with the atomic
// clear / set aliases: the two cores write their own bytes.
#define HANG_MAGIC 0x4bu // 'K'
#define HANG_PANIC_MAGIC 0x50u // 'P'
#define HANG_HALT_MAGIC 0x48u // 'H'
#define HANG_APP_MS 8000  // a core load with big roms takes a few seconds
#define HANG_WDT_MS 1500  // core0 does not run the watchdog task for this long
#define HANG_TASK_PRIORITY (configMAX_PRIORITIES - 2)

static volatile bool armed = false;
static volatile uint32_t app_alive_ms = 0;

static uint8_t task_tag() {
  if (__get_current_exception() != 0) return '!';
  if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return '-';
  const char *name = pcTaskGetName(NULL);
  return name && name[0] ? name[0] : '?';
}

static volatile uint8_t beat_last[4] = { 0xff, 0xff, 0xff, 0xff }; // one writer each

// in RAM, as the USB loop that calls it; the inlined divider, not the division of the sdk in flash
static void __not_in_flash_func(beat)(uint8_t i) {
  uint8_t s = (uint8_t) hw_divider_u32_quotient_inlined(time_us_32(), 1000000);
  if (beat_last[i] == s) return; // the loops run many times a second
  beat_last[i] = s;
  hw_clear_bits(&watchdog_hw->scratch[3], 0xffu << (8 * i));
  hw_set_bits(&watchdog_hw->scratch[3], (uint32_t) s << (8 * i));
}

void hang_mark(uint16_t where) {
  watchdog_hw->scratch[1] = ((uint32_t) HANG_MAGIC << 24) | where;
  app_alive_ms = ms_now();
  beat(1);
}

void __not_in_flash_func(hang_beat_core1)() { if (armed) beat(2); } // the USB task starts before the report is read

static repeating_timer_t irq_timer;
static bool irq_beat(repeating_timer_t *t) { beat(3); return true; }

void hang_step(uint8_t step) {
  hang_mark((watchdog_hw->scratch[1] & 0xff00) | step);
}

// one byte of scratch 2: 3 SPI holder, 2 SPI waiter, 1 I2C holder, 0 I2C waiter
static void set_byte(uint8_t i, uint8_t v) {
  uint32_t save = save_and_disable_interrupts();
  uint32_t s = watchdog_hw->scratch[2];
  s = (s & ~(0xffu << (8 * i))) | ((uint32_t) v << (8 * i));
  watchdog_hw->scratch[2] = s;
  restore_interrupts(save);
}

static uint8_t get_byte(uint8_t i) { return (watchdog_hw->scratch[2] >> (8 * i)) & 0xff; }

void hang_lock_wait(uint8_t lock) { set_byte(lock == HANG_LOCK_SPI ? 2 : 0, task_tag()); }

void hang_lock_held(uint8_t lock) {
  uint8_t t = task_tag();
  uint8_t wait = lock == HANG_LOCK_SPI ? 2 : 0;
  if (get_byte(wait) == t) set_byte(wait, 0);
  set_byte(wait + 1, t);
}

void hang_lock_free(uint8_t lock, bool still_held) {
  if (!still_held) set_byte(lock == HANG_LOCK_SPI ? 3 : 1, 0);
}

static char tag_char(uint8_t t) { return t ? (char) t : '-'; }

void hang_diag_report() {
  uint32_t mark = watchdog_hw->scratch[1];
  uint32_t locks = watchdog_hw->scratch[2];
  uint32_t times = watchdog_hw->scratch[3];
  if ((mark >> 24) == HANG_HALT_MAGIC) {
    const char *msg = (const char *) locks;
    bool in_flash = locks >= XIP_BASE && locks < XIP_BASE + PICO_FLASH_SIZE_BYTES;
    d_printf("Last boot halted: \"%s\"; APP at %04lx", in_flash ? msg : "?", (unsigned long) (mark & 0xffff));
    d_println();
  } else if ((mark >> 24) == HANG_PANIC_MAGIC) {
    const char *msg = (const char *) locks;
    bool in_flash = locks >= XIP_BASE && locks < XIP_BASE + PICO_FLASH_SIZE_BYTES;
    d_printf("Last boot panicked: \"%s\" in %s %lu; APP at %04lx",
      in_flash ? msg : "?", ((mark >> 16) & 0xff) ? "exception" : "a task",
      (unsigned long) ((mark >> 16) & 0xff), (unsigned long) (mark & 0xffff));
    d_println();
  } else if (watchdog_enable_caused_reboot() && (mark >> 24) == HANG_MAGIC) {
    d_printf("Last boot hung: APP at %04lx (tick %lu, step %02lx); last ran at (s, mod 256): APP %lu, "
             "watchdog task %lu, core1 USB loop %lu, core0 timer irq %lu; "
             "SPI held by %c, waited by %c; I2C held by %c, waited by %c",
      (unsigned long) (mark & 0xffff), (unsigned long) ((mark >> 8) & 0xff), (unsigned long) (mark & 0xff),
      (unsigned long) ((times >> 8) & 0xff), (unsigned long) (times & 0xff),
      (unsigned long) ((times >> 16) & 0xff), (unsigned long) (times >> 24),
      tag_char((locks >> 24) & 0xff), tag_char((locks >> 16) & 0xff),
      tag_char((locks >> 8) & 0xff), tag_char(locks & 0xff));
    d_println();
  }
  watchdog_hw->scratch[1] = 0;
  watchdog_hw->scratch[2] = 0;
  watchdog_hw->scratch[3] = 0;
}

// halt() (also with debug=0, before karabas.ini is read): the message for the next boot
void hang_diag_halt(const char *msg) {
  watchdog_hw->scratch[2] = (uint32_t) msg;
  watchdog_hw->scratch[1] = ((uint32_t) HANG_HALT_MAGIC << 24) | (watchdog_hw->scratch[1] & 0xffff);
}

// every panic (-Wl,--wrap=panic): TinyUSB's driver panics in the USB irq, and so do FreeRTOS
// asserts (rtosFatalError). Its message goes nowhere, so it is kept for the next boot, and with
// the watchdog on the board reboots right away.
void __real_panic(const char *fmt, ...) __attribute__((noreturn));
void __attribute__((noreturn)) __wrap_panic(const char *fmt, ...) {
  watchdog_hw->scratch[2] = (uint32_t) fmt;
  watchdog_hw->scratch[1] = ((uint32_t) HANG_PANIC_MAGIC << 24) | ((__get_current_exception() & 0xff) << 16) |
    (watchdog_hw->scratch[1] & 0xffff);
  if (armed) watchdog_reboot(0, 0, 10);
  __real_panic("%s", fmt);
}

static void hang_task(void *param) {
  for (;;) {
    beat(0);
    if (armed && ms_now() - app_alive_ms < HANG_APP_MS) watchdog_update();
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void hang_diag_stop() {
  armed = false;
  watchdog_disable();
}

void hang_diag_start() {
  if (armed) return;
  armed = true;
  app_alive_ms = ms_now();
  add_repeating_timer_ms(100, irq_beat, NULL, &irq_timer); // on core0: APP runs this
  watchdog_enable(HANG_WDT_MS, true);
  if (xTaskCreateAffinitySet(hang_task, "WDOG", 256, NULL, HANG_TASK_PRIORITY, 1 << 0, NULL) != pdPASS) {
    watchdog_disable();
    d_println("Hang diag: no task, the watchdog is off");
    return;
  }
  d_println("Hang diag: the watchdog is on");
}
