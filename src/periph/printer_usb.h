#pragma once

#include "usb/printer_host.h"

// Internal entry points: USB task only. APP uses printer.h instead.
void printer_usb_tick();
void printer_usb_event(PrinterEvent event, xfer_result_t result, uint32_t count);
