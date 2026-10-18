#include <Arduino.h>
#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"
#include "xinput_host.h"
#include "printer_host.h"
#include "config.h"
#include "storage/settings.h"
#include "periph/printer_usb.h"

extern "C" usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *count) {
  static const usbh_class_driver_t drivers[] = { usbh_xinput_driver, usbh_printer_driver };
  *count = sizeof(drivers) / sizeof(drivers[0]);
  return drivers;
}

extern "C" bool tuh_enum_descriptor_configuration_cb(uint8_t address, uint8_t index,
                                                       const tusb_desc_configuration_t *) {
  printer_host_config_index(address, index);
  return true;
}

void printer_host_event(PrinterEvent event, xfer_result_t result, uint32_t count, const uint8_t *) {
  printer_usb_event(event, result, count);
  static bool status_logged = false;
  static uint8_t last_status = 0;
  const auto &p = printer_host_info();
  if (event == PrinterEvent::Mounted) {
    status_logged = false;
    d_printf("Printer mounted %04x:%04x address=%u interface=%u protocol=%u OUT=%02x IN=%02x\r\n",
             p.vid, p.pid, p.address, p.interface_number, p.protocol, p.ep_out, p.ep_in);
  } else if (event == PrinterEvent::Unmounted) {
    d_printf("Printer disconnected address=%u\r\n", p.address);
  } else if (event == PrinterEvent::DeviceId && p.id_valid) {
    d_printf("Printer ID%s: %s\r\n", p.id_truncated ? " (truncated)" : "", p.device_id);
  } else if (event == PrinterEvent::Status && p.status_valid) {
    if (!status_logged || last_status != p.port_status) {
      d_printf("Printer status=%02x paper_empty=%u selected=%u error=%u\r\n", p.port_status,
               (p.port_status >> 5) & 1, (p.port_status >> 4) & 1, !(p.port_status & 8));
      last_status = p.port_status;
      status_logged = true;
    }
  } else if (event == PrinterEvent::Write && result == XFER_RESULT_SUCCESS) {
    d_printf("Printer USB write complete: %lu bytes\r\n", (unsigned long)count);
  } else if (result != XFER_RESULT_SUCCESS) {
    if (event == PrinterEvent::Status) status_logged = false;
    d_printf("Printer event=%u result=%u transferred=%lu\r\n",
             unsigned(event), unsigned(result), (unsigned long)count);
  }
}
