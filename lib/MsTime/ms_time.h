#pragma once
// Milliseconds since boot, in place of ElapsedTimer and Arduino's millis / micros / delay:
// a timer is a uint32_t start time.
//   uint32_t t = ms_now();  ...  if (ms_since(t) > 500) { t = ms_now(); ... }
// The count wraps after 49 days; ms_since() is right across the wrap.

#include <stdint.h>
#include "pico/time.h"
#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t ms_now(void) {
  return to_ms_since_boot(get_absolute_time());
}

static inline uint32_t ms_since(uint32_t start) {
  return ms_now() - start;
}

// lets the other tasks run for ms (Arduino's delay() under FreeRTOS); 1 ms ticks
static inline void ms_delay(uint32_t ms) {
  if (ms) vTaskDelay(pdMS_TO_TICKS(ms));
}

static inline uint32_t us_now(void) {
  return time_us_32();
}

#ifdef __cplusplus
}
#endif
