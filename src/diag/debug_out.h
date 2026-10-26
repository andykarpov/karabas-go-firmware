#pragma once

#include <Arduino.h>

// Debug output (d_print* macros in config.h). Only one context may write to the USB CDC
// Serial: Adafruit's CDC write has no lock, and concurrent writers corrupt the TinyUSB device
// state until the host resets the port. Everything goes to a ring buffer; the owner (the APP
// task) sends from it what the CDC fifo takes at once, on its own writes and in debug_drain()
// from the loop. It never waits for the host.

Print &debug_out();
void debug_flush();
void debug_drain();
void debug_discard(); // drops what is buffered and not sent yet
void debug_set_owner_task(void *task); // the task that owns Serial (APP)
