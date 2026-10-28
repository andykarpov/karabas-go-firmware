#pragma once

#include <stdarg.h>
#include <stddef.h>

// Debug output (d_print* macros in config.h), from any task, core or irq. Everything goes to a
// ring buffer; the owner (the APP task, which also runs the serial bridge on the same CDC port)
// sends from it what the CDC fifo takes at once, on its own writes and in debug_drain() from the
// loop, so the lines of different writers are not mixed. It never waits for the host. Until a
// terminal opens the port, the buffer keeps the boot log (8 KB).

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
void debug_set_owner_task(void *task); // the task that writes to the CDC (APP)

#ifdef __cplusplus
}
#endif
