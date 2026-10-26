#include "main.h"
#include "usb_device.h"
#include "tusb.h"
#include "pico/mutex.h"

#include <FreeRTOS.h>
#include <task.h>

// Adafruit's rp2040 port runs tud_task() in a soft irq after every USB irq. TinyUSB guards its
// endpoints and fifos with pico mutexes, and the pico-sdk of arduino-pico is built without the
// FreeRTOS interop: a mutex belongs to a core, and a wait is a wfe loop. An irq that waits for
// one held by the task it interrupted never returns, and core0 stops with its timer irqs.
// So the irq only wakes this task (patches/4-adafruit-tinyusb-rp2040-port.patch), and
// tud_task() runs here under __usb_mutex, like the task-side CDC calls
// (patches/3-adafruit-usbd-cdc.patch, debug_drain).
// In MSC mode the read / write callbacks (the SD card) run in this task too.

#define USBD_TASK_STACK 1024 // words: the MSC callbacks read the SD card
#define USBD_TASK_PRIORITY (configMAX_PRIORITIES / 2 + 1) // above APP: enumeration while APP loads a core

extern mutex_t __usb_mutex;
static TaskHandle_t usbd_task_handle = nullptr;

// called by usb_task_irq (patched port): true leaves tud_task() to the task
extern "C" bool TinyUSB_Port_DeviceTaskHook(void) {
  if (usbd_task_handle == nullptr) return false;
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(usbd_task_handle, &woken);
  portYIELD_FROM_ISR(woken);
  return true;
}

static void usbd_task(void *param) {
  bool missed = false;
  for (;;) {
    // events wait in TinyUSB's queue; when APP held the mutex, try again on the next tick
    ulTaskNotifyTake(pdTRUE, missed ? 1 : portMAX_DELAY);
    missed = !mutex_try_enter(&__usb_mutex, NULL);
    if (!missed) {
      tud_task();
      mutex_exit(&__usb_mutex);
    }
  }
}

void usb_device_task_start() {
  // pinned to core0 with the USB irq; the irq runs tud_task() itself until the handle is set
  TaskHandle_t h;
  if (xTaskCreateAffinitySet(usbd_task, "USBD", USBD_TASK_STACK, nullptr, USBD_TASK_PRIORITY, 1 << 0, &h) != pdPASS) {
    panic("Unable to create USBD task");
  }
  usbd_task_handle = h;
  xTaskNotifyGive(h); // events that came before
}
