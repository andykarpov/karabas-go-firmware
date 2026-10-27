#pragma once
// The chip itself on the pico-sdk: reboots and the free heap (as arduino-pico's rp2040.*).

#include <malloc.h>
#include "hardware/watchdog.h"
#include "pico/bootrom.h"

static inline void chip_reboot(void) {
  watchdog_reboot(0, 0, 10);
  while (1) continue;
}

// into the USB bootloader (BOOTSEL)
static inline void chip_reboot_bootsel(void) {
  reset_usb_boot(0, 0);
  while (1) continue;
}

// the heap between the end of bss and the stack limit, less what malloc has given out
static inline int chip_free_heap(void) {
  extern char __StackLimit, __bss_end__;
  struct mallinfo m = mallinfo();
  return (int) (&__StackLimit - &__bss_end__) - m.uordblks;
}
