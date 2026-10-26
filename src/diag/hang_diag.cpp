#include "main.h"
#include "hang_diag.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#include <FreeRTOS.h>
#include <task.h>

// scratch 1: magic << 24 | APP mark
// scratch 2: SPI holder, SPI waiter, I2C holder, I2C waiter (task tags, one byte each)
// scratch 3: the second (since boot, 16 bit) APP last marked << 16 | the watchdog task last ran
#define HANG_MAGIC 0x4bu // 'K'
#define HANG_APP_MS 8000  // a core load with big roms takes a few seconds
#define HANG_WDT_MS 1500  // core0 does not run the watchdog task for this long
#define HANG_TASK_PRIORITY (configMAX_PRIORITIES - 2)

static volatile bool armed = false;
static volatile uint32_t app_alive_ms = 0;

static uint8_t task_tag() {
  if (__get_current_exception() != 0) return '!';
  if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return '-';
  const char *name = pcTaskGetName(nullptr);
  return name && name[0] ? name[0] : '?';
}

static uint16_t seconds() { return (uint16_t) (millis() / 1000); }

void hang_mark(uint16_t where) {
  watchdog_hw->scratch[1] = ((uint32_t) HANG_MAGIC << 24) | where;
  app_alive_ms = millis();
  watchdog_hw->scratch[3] = (watchdog_hw->scratch[3] & 0xffff) | ((uint32_t) seconds() << 16);
}

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
  if (watchdog_enable_caused_reboot() && (mark >> 24) == HANG_MAGIC) {
    d_printf("Last boot hung: APP at %04lx (tick %lu, step %lu), last marked at %lu s, "
             "the watchdog task last ran at %lu s; SPI held by %c, waited by %c; I2C held by %c, waited by %c",
      (unsigned long) (mark & 0xffff), (unsigned long) ((mark >> 8) & 0xff), (unsigned long) (mark & 0xff),
      (unsigned long) (times >> 16), (unsigned long) (times & 0xffff),
      tag_char((locks >> 24) & 0xff), tag_char((locks >> 16) & 0xff),
      tag_char((locks >> 8) & 0xff), tag_char(locks & 0xff));
    d_println();
  }
  watchdog_hw->scratch[1] = 0;
  watchdog_hw->scratch[2] = 0;
  watchdog_hw->scratch[3] = 0;
}

static void hang_task(void *param) {
  for (;;) {
    watchdog_hw->scratch[3] = (watchdog_hw->scratch[3] & 0xffff0000u) | seconds();
    if (armed && millis() - app_alive_ms < HANG_APP_MS) watchdog_update();
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
  app_alive_ms = millis();
  watchdog_enable(HANG_WDT_MS, true);
  if (xTaskCreateAffinitySet(hang_task, "WDOG", 256, nullptr, HANG_TASK_PRIORITY, 1 << 0, nullptr) != pdPASS) {
    watchdog_disable();
    d_println("Hang diag: no task, the watchdog is off");
    return;
  }
  d_println("Hang diag: the watchdog is on");
}
