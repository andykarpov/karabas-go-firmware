#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// The USB device stack (CDC serial, MSC) runs in the USBD task on core0 (usb_device.c).
void usb_device_task_start();

#ifdef __cplusplus
}
#endif
