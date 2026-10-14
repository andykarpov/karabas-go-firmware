#pragma once

#include <Arduino.h>

// Debug output (d_print* macros in config.h). Only one context may write to the USB CDC
// Serial: Adafruit's CDC write has no lock, and concurrent writers corrupt the TinyUSB device
// state until the host resets the port. The owner (core0 main loop, or the APP task under
// FreeRTOS) writes directly; everything else (core1, other tasks, irqs) goes to a ring buffer
// that the owner drains with debug_drain().

Print &debug_out();
void debug_flush();
void debug_drain();
void debug_set_owner_task(void *task); // FreeRTOS: the task that owns Serial (APP)
