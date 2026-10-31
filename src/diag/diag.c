#include "main.h"
#include "diag.h"
#include "usb_diag.h"
#include "usb/midi_host.h"

#if DIAG_STATS

#include <FreeRTOS.h>
#include <task.h>
#include "hardware/structs/xip_ctrl.h"
#include "hardware/clocks.h"

#define DIAG_PERIOD_MS 10000

typedef struct {
  volatile uint32_t loops;
  volatile uint32_t max_gap_us;
  uint32_t last_us;
} diag_core_t;

static diag_core_t diag[2];

static inline void diag_loop(diag_core_t *d) {
  uint32_t now = time_us_32();
  if (d->last_us != 0) {
    uint32_t gap = now - d->last_us;
    if (gap > d->max_gap_us) d->max_gap_us = gap;
  }
  d->last_us = now;
  d->loops++;
}

void diag_loop_core0() { diag_loop(&diag[0]); }
void __not_in_flash_func(diag_loop_core1)() { diag_loop(&diag[1]); } // the USB loop runs from RAM

void diag_tick() {
  static uint32_t timer = 0;
  if (ms_since(timer) < DIAG_PERIOD_MS) return;
  uint32_t elapsed = ms_since(timer);
  timer = ms_now();

  uint32_t loops0 = diag[0].loops, gap0 = diag[0].max_gap_us;
  uint32_t loops1 = diag[1].loops, gap1 = diag[1].max_gap_us;
  diag[0].loops = 0; diag[0].max_gap_us = 0;
  diag[1].loops = 0; diag[1].max_gap_us = 0;

  d_printf("DIAG core0: %lu loops/s, max gap %lu us | core1: %lu loops/s, max gap %lu us | heap free %d",
    (unsigned long) (loops0 * 1000ULL / elapsed), (unsigned long) gap0,
    (unsigned long) (loops1 * 1000ULL / elapsed), (unsigned long) gap1,
    chip_free_heap());
  // the flash cache: both cores run from it, misses stall them
  uint32_t hit = xip_ctrl_hw->ctr_hit, acc = xip_ctrl_hw->ctr_acc;
  xip_ctrl_hw->ctr_hit = 0; xip_ctrl_hw->ctr_acc = 0; // a write clears a counter
  d_printf(" | xip hit %lu.%lu%% of %lu k/s, clk %lu MHz", (unsigned long) (acc ? hit * 1000ULL / acc / 10 : 0),
    (unsigned long) (acc ? hit * 1000ULL / acc % 10 : 0), (unsigned long) (acc / elapsed),
    (unsigned long) (clock_get_hz(clk_sys) / 1000000));
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
  usb_diag_print();
  midi_host_stats_t midi;
  midi_host_stats(&midi);
  if (midi.mounted || midi.rx) {
    d_printf(" | midi %s rx %lu sent %lu skipped %lu", midi.mounted ? "on" : "off", (unsigned long) midi.rx,
      (unsigned long) midi.sent, (unsigned long) midi.skipped);
  }
  usb_uart_stats_t uart;
  usb_uart_stats(&uart);
  if (uart.mounted || uart.rx || uart.tx) {
    d_printf(" | uart %s %lu rx %lu tx %lu lost %lu/%lu", uart.mounted ? "on" : "off", (unsigned long) uart.baud,
      (unsigned long) uart.rx, (unsigned long) uart.tx, (unsigned long) uart.rx_lost, (unsigned long) uart.tx_lost);
  }
  d_println();
}

#endif // DIAG_STATS
