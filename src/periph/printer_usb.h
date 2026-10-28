#pragma once

#include "usb/printer_host.h"

#ifdef __cplusplus
extern "C" {
#endif

// Internal entry points: USB task only. APP uses printer.h instead.
void printer_usb_tick();
void printer_usb_event(printer_event_t event, xfer_result_t result, uint32_t count);

#ifdef __cplusplus
}
#endif
