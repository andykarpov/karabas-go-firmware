#pragma once

#include "tusb.h"
#include "host/usbh_pvt.h"

// All API calls, snapshots and callbacks belong to the USB task (core1).
// No cross-core queue or FPGA printing service is provided by this driver.
constexpr uint16_t PRINTER_TRANSFER_SIZE = 512;
constexpr uint16_t PRINTER_ID_SIZE = 256;

enum class PrinterEvent { Mounted, Unmounted, DeviceId, Status, Write, Read, Reset };

struct PrinterInfo {
  uint8_t address, interface_number, protocol, ep_out, ep_in;
  uint16_t vid, pid;
  bool mounted, tx_busy, rx_busy, fault, status_valid, id_valid, id_truncated;
  uint8_t port_status; // bit 5: paper empty; bit 4: selected; bit 3: no error
  char device_id[PRINTER_ID_SIZE - 1]; // excludes the two-byte IEEE-1284 length
};

extern usbh_class_driver_t const usbh_printer_driver;
const PrinterInfo &printer_host_info();
void printer_host_tick(uint32_t now_ms);
void printer_host_config_index(uint8_t address, uint8_t index);

// true = submitted, false = busy/disconnected/fault/invalid input; never blocks.
// Data is copied into driver storage. Completion is reported through the callback.
bool printer_host_write(const uint8_t *data, uint16_t length);
bool printer_host_read(); // protocol 2 only; no automatic Bulk IN polling
bool printer_host_get_id();
bool printer_host_get_status();
// Destructive, explicit recovery only. Rejected while a bulk transfer is active.
// legacy=true selects the USB Printer 1.0 recipient quirk (0x23).
bool printer_host_soft_reset(bool legacy = false);

// Optional application hook. Read data and successful Write data are valid only
// for the duration of callback. Do not consume Write data on a failed transfer.
// A successful USB write does not imply physical printing. No automatic retries.
void printer_host_event(PrinterEvent event, xfer_result_t result,
                        uint32_t transferred, const uint8_t *data);
