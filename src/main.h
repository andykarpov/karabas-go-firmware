#pragma once

// Umbrella header for the firmware modules. Module state is owned by the module
// that defines it; the declarations live in the module headers included below.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <PioSPI.h>
#include "config.h"
#include "types.h"
#include "FT812.h"
#include "OSD.h"
#include "RTC.h"
#include "SdFat.h"
#include "SegaController.h"
#include "hid_app.h"
#include "ms_time.h"
#include "file.h"
#include "EspSerial.h"
#include "MultiMatrixDisplay.h"
#include "board/board.h"
#include "fpga/link.h"
#include "fpga/bitstream.h"
#include "fpga/core_loader.h"
#include "input/input.h"
#include "periph/matrix.h"
#include "periph/oled.h"
#include "periph/serial_bridge.h"
#include "periph/esp_at.h"
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

// main.cpp
extern RTC zxrtc;
extern EspSerial esp_serial;
extern volatile bool usb_host_ready; // set by core1 once PIO-USB has claimed its PIO/DMA resources
void halt(const char* msg);

// hid_app.cpp / hid_driver.cpp
extern hid_keyboard_report_t usb_keyboard_report;
extern hid_mouse_report_t usb_mouse_report;

// ui/file_list.cpp
extern file_list_sort_item_t *files;
extern uint16_t files_len;
void files_clear();                       // empty the list (frees a large buffer)
file_list_sort_item_t *files_append();    // zeroed slot for a new entry, nullptr if full / no memory
extern uint16_t file_sel;
extern uint16_t file_page_size;
extern uint16_t file_pages;
extern uint16_t file_page;
extern file_list_item_t cached_names[MAX_CORES_PER_PAGE];
extern uint16_t cached_file_from, cached_file_to;
