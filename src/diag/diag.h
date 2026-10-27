#pragma once

// Runtime diagnostics (build with -D DIAG_STATS=1, printed with debug=1 in karabas.ini):
// loop rate and worst loop gap per core, free heap and task stack headroom.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DIAG_STATS
#define DIAG_STATS 0
#endif

#if DIAG_STATS
void diag_loop_core0();
void diag_loop_core1();
void diag_tick();
#else
static inline void diag_loop_core0(void) {}
static inline void diag_loop_core1(void) {}
static inline void diag_tick(void) {}
#endif

#ifdef __cplusplus
}
#endif
