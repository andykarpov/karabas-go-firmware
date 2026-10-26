#pragma once

// The USB device stack (CDC serial, MSC) runs in the USBD task on core0, not in an irq: see
// usb_device.cpp. Task-side TinyUSB device calls still take __usb_mutex (mutex_try_enter).
void usb_device_task_start();
