#pragma once

// USB diagnostics (printed with debug=1): the root port events of the PIO-USB host, logged from
// core1, and a summary for the DIAG line.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void usb_diag_tick();           // core1, from the USB loop
void usb_diag_print();          // appends the summary to the DIAG line

#ifdef __cplusplus
}
#endif
