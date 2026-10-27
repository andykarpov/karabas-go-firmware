#pragma once

// USB diagnostics (printed with debug=1): the root port events of the PIO-USB host, logged from
// core1, and a summary for the DIAG line.
// Not CFG_TUSB_DEBUG: Adafruit's log_printf then starts Serial1 (UART0 on GPIO 0 / 1, the SD card
// pins of the Mini boards) at the first message.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void usb_diag_tick();           // core1, from the USB loop
void usb_diag_print();          // appends the summary to the DIAG line
void usb_diag_device_missed();  // the USBD task found __usb_mutex taken

#ifdef __cplusplus
}
#endif
