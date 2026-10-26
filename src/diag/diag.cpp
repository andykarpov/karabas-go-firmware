#include "main.h"
#include "diag.h"

#if DIAG_STATS

#include <FreeRTOS.h>
#include <task.h>

#define DIAG_PERIOD_MS 10000

typedef struct {
  volatile uint32_t loops;
  volatile uint32_t max_gap_us;
  uint32_t last_us;
} diag_core_t;

static diag_core_t diag[2];

static inline void diag_loop(diag_core_t &d) {
  uint32_t now = time_us_32();
  if (d.last_us != 0) {
    uint32_t gap = now - d.last_us;
    if (gap > d.max_gap_us) d.max_gap_us = gap;
  }
  d.last_us = now;
  d.loops++;
}

void diag_loop_core0() { diag_loop(diag[0]); }
void diag_loop_core1() { diag_loop(diag[1]); }

void diag_tick() {
  static ElapsedTimer timer;
  if (timer.elapsed() < DIAG_PERIOD_MS) return;
  uint32_t elapsed = timer.elapsed();
  timer.reset();

  uint32_t loops0 = diag[0].loops, gap0 = diag[0].max_gap_us;
  uint32_t loops1 = diag[1].loops, gap1 = diag[1].max_gap_us;
  diag[0].loops = 0; diag[0].max_gap_us = 0;
  diag[1].loops = 0; diag[1].max_gap_us = 0;

  d_printf("DIAG core0: %lu loops/s, max gap %lu us | core1: %lu loops/s, max gap %lu us | heap free %d",
    (unsigned long) (loops0 * 1000ULL / elapsed), (unsigned long) gap0,
    (unsigned long) (loops1 * 1000ULL / elapsed), (unsigned long) gap1,
    rp2040.getFreeHeap());
  TaskHandle_t app = xTaskGetHandle("APP");
  TaskHandle_t usb = xTaskGetHandle("USB");
  TaskHandle_t input = xTaskGetHandle("INPUT");
  TaskHandle_t matrix = xTaskGetHandle("MATRIX");
  TaskHandle_t oled = xTaskGetHandle("OLED");
  TaskHandle_t usbd = xTaskGetHandle("USBD");
  d_printf(" | stack free APP %lu USB %lu USBD %lu INPUT %lu MATRIX %lu OLED %lu words",
    (unsigned long) (app ? uxTaskGetStackHighWaterMark(app) : 0),
    (unsigned long) (usb ? uxTaskGetStackHighWaterMark(usb) : 0),
    (unsigned long) (usbd ? uxTaskGetStackHighWaterMark(usbd) : 0),
    (unsigned long) (input ? uxTaskGetStackHighWaterMark(input) : 0),
    (unsigned long) (matrix ? uxTaskGetStackHighWaterMark(matrix) : 0),
    (unsigned long) (oled ? uxTaskGetStackHighWaterMark(oled) : 0));
  d_println();
}

#endif // DIAG_STATS
