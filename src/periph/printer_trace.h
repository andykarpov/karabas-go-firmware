#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Each trace belongs to one task. Emit whole lines, not one log call per
// byte: this reduces log traffic and prevents RX/TX text from interleaving.
// Offsets reveal lost log lines. Hex preserves ESC, CR, LF and high-bit bytes.
typedef void (*printer_trace_sink_t)(uint32_t offset, const char *hex);

typedef struct {
  printer_trace_sink_t sink;
  uint32_t offset;
  size_t used;
  char hex[96];
} printer_trace_t;

static inline void printer_trace_reset(printer_trace_t *t) {
  t->offset = 0;
  t->used = 0;
}

static inline void printer_trace_flush(printer_trace_t *t) {
  if (!t->used) return;
  t->hex[t->used * 3 - 1] = 0;
  t->sink(t->offset, t->hex);
  t->offset += t->used;
  t->used = 0;
}

static inline void printer_trace_append(printer_trace_t *t, const uint8_t *data, size_t length) {
  static const char digits[] = "0123456789ABCDEF";
  for (size_t i = 0; i < length; ++i) {
    const size_t pos = t->used * 3;
    t->hex[pos] = digits[data[i] >> 4];
    t->hex[pos + 1] = digits[data[i] & 15];
    t->hex[pos + 2] = ' ';
    if (++t->used == 32) printer_trace_flush(t);
  }
}

#ifdef __cplusplus
}
#endif
