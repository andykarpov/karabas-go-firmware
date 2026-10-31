#pragma once

// Umbrella header for the firmware modules. Module state is owned by the module
// that defines it; the declarations live in the module headers included below.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "i2c_dev.h"
#include "config.h"
#include "types.h"
#include "ft812.h"
#include "osd.h"
#include "rtc.h"
#include "input/sega.h"
#include "usb/hid_app.h"
#include "ms_time.h"
#include "storage/file.h"
#include "board/board.h"
#include "board/pin.h"
#include "board/chip.h"
#include "fpga/link.h"
#include "fpga/bitstream.h"
#include "fpga/core_loader.h"
#include "input/input.h"
#include "periph/matrix.h"
#include "periph/oled.h"
#include "periph/serial_bridge.h"
#include "periph/usb_uart.h"
#include "periph/esp_at.h"
#include "periph/esp_serial.h"
#include "periph/ntp_sync.h"
#include "periph/esp_update.h"
#include "periph/periph.h"
#include "periph/i2c_bus.h"
#include "storage/storage.h"
#include "storage/settings.h"
#include "storage/msc.h"
#include "storage/img_mount.h"
#include "storage/bk_save.h"
#include "storage/sd2.h"
#include "usb/usb_device.h"
#include "ui/osd_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// main.cpp
extern volatile bool usb_host_ready; // set by core1 once PIO-USB has claimed its PIO/DMA resources
void halt(const char* msg);

// usb/hid_app.c, usb/hid_driver.c
extern hid_keyboard_report_t usb_keyboard_report;
extern hid_mouse_report_t usb_mouse_report;


#ifdef __cplusplus
}
#endif
