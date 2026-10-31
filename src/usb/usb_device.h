#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// The USB device stack (CDC serial, MSC) runs in the USBD task on core0 (usb_device.c).
void usb_device_task_start();
// the 1200 baud touch of the CDC port asked for BOOTSEL: APP does the reboot (serial_bridge_tick)
bool usb_device_bootsel_requested();

#ifdef __cplusplus
}
#endif
