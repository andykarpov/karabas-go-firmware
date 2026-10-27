#pragma once
// Milliseconds since boot, in place of ElapsedTimer: a timer is a uint32_t start time.
//   uint32_t t = ms_now();  ...  if (ms_since(t) > 500) { t = ms_now(); ... }
// The count wraps after 49 days; ms_since() is right across the wrap.

#include <stdint.h>
#include "pico/time.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t ms_now(void) {
  return to_ms_since_boot(get_absolute_time());
}

static inline uint32_t ms_since(uint32_t start) {
  return ms_now() - start;
}

#ifdef __cplusplus
}
#endif
