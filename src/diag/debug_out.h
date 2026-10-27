#pragma once

#include <stdarg.h>
#include <stddef.h>

// Debug output (d_print* macros in config.h). Only one context may write to the USB CDC
// Serial: Adafruit's CDC write has no lock, and concurrent writers corrupt the TinyUSB device
// state until the host resets the port. Everything goes to a ring buffer; the owner (the APP
// task) sends from it what the CDC fifo takes at once, on its own writes and in debug_drain()
// from the loop. It never waits for the host.

#ifdef __cplusplus
extern "C" {
#endif

void debug_write(const char *data, size_t len);
void debug_print(const char *s);
void debug_println(const char *s, ...); // s NULL: only the line end (the d_println macro)
void debug_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void debug_vprintf(const char *fmt, va_list ap);
void debug_flush(void);
void debug_drain(void);
void debug_discard(void); // drops what is buffered and not sent yet
void debug_set_owner_task(void *task); // the task that owns Serial (APP)

#ifdef __cplusplus
}
#endif
