#pragma once

// Runtime diagnostics (build with -D DIAG_STATS=1, printed with debug=1 in karabas.ini):
// loop rate and worst loop gap per core, free heap and task stack headroom.

#include <Arduino.h>

#ifndef DIAG_STATS
#define DIAG_STATS 0
#endif

#if DIAG_STATS
void diag_loop_core0();
void diag_loop_core1();
void diag_tick();
#else
inline void diag_loop_core0() {}
inline void diag_loop_core1() {}
inline void diag_tick() {}
#endif
