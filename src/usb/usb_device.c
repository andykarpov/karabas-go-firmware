#include "main.h"
#include "usb_device.h"
#include "tusb.h"
#include "pico/bootrom.h"

#include <FreeRTOS.h>
#include <task.h>

// The USB device stack (CDC, MSC) runs in the USBD task: tud_task() blocks on TinyUSB's queue,
// the USB irq only queues the events. TinyUSB runs on OPT_OS_FREERTOS, its fifos are guarded by
// FreeRTOS mutexes: APP writes to the CDC (debug_out, serial_bridge) with no lock of its own.
// In MSC mode the read / write callbacks (the SD card) run in this task too.

#define USBD_TASK_STACK 1024 // words: the MSC callbacks read the SD card
#define USBD_TASK_PRIORITY (configMAX_PRIORITIES / 2 + 1) // above APP: enumeration while APP loads a core

static void usbd_task(void *param) {
  (void) param;
  // here, on core0: the USB irq goes to the core that starts the stack
  tusb_rhport_init_t dev_init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO };
  tusb_init(BOARD_TUD_RHPORT, &dev_init);
  for (;;) {
    tud_task(); // blocks until an event
  }
}

void usb_device_task_start() {
  if (xTaskCreateAffinitySet(usbd_task, "USBD", USBD_TASK_STACK, NULL, USBD_TASK_PRIORITY, 1 << 0, NULL) != pdPASS) {
    panic("Unable to create USBD task");
  }
}

// the 1200 baud touch of the upload tools (pio upload, arduino-cli) reboots into BOOTSEL.
// APP does it (the saves, the SD1 transfer: the card is APP's); a hung APP still gets there
// from here after a second.
#define BOOTSEL_REQUEST_TIMEOUT_MS 1000
static volatile bool bootsel_request = false;

bool usb_device_bootsel_requested() {
  return bootsel_request;
}

void tud_cdc_line_coding_cb(uint8_t itf, const cdc_line_coding_t *coding) {
  (void) itf;
  if (coding->bit_rate != 1200) return;
  bootsel_request = true;
  uint32_t start = ms_now();
  while (ms_since(start) < BOOTSEL_REQUEST_TIMEOUT_MS) vTaskDelay(pdMS_TO_TICKS(10));
  chip_reboot_bootsel();
}
