#pragma once

// USB diagnostics (printed with debug=1): the root port events of the PIO-USB host, logged from
// core1, and a summary for the DIAG line. TinyUSB's error log (CFG_TUSB_DEBUG=1, its printf goes
// through -Wl,--wrap=printf) goes to d_print too.

#include <Arduino.h>

void usb_diag_tick();           // core1, from the USB loop
void usb_diag_print();          // appends the summary to the DIAG line
void usb_diag_device_missed();  // the USBD task found __usb_mutex taken
