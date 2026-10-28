#include <stdbool.h>
#include <stdint.h>
#include "pio_usb.h"
#include <string.h>
#include "tusb.h"
#include "xinput_host.h"
#include "printer_host.h"
#include "config.h"
#include "storage/settings.h"
#include "periph/printer_usb.h"
#include "periph/printer_trace.h"

static void trace_tx(uint32_t offset, const char *hex) {
  d_printf("Printer TX offset=%lu hex=%s\r\n", (unsigned long)offset, hex);
}
static printer_trace_t tx_trace = { trace_tx }; // USB task only, separate from APP's RX trace

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *count) {
  // C has no constant struct copies: the table is filled at the first call
  static usbh_class_driver_t drivers[2];
  memcpy(&drivers[0], &usbh_xinput_driver, sizeof(drivers[0]));
  memcpy(&drivers[1], &usbh_printer_driver, sizeof(drivers[1]));
  *count = sizeof(drivers) / sizeof(drivers[0]);
  return drivers;
}

bool tuh_enum_descriptor_configuration_cb(uint8_t address, uint8_t index,
                                                       const tusb_desc_configuration_t *desc) {
  (void) desc;
  printer_host_config_index(address, index);
  return true;
}

void printer_host_event(printer_event_t event, xfer_result_t result, uint32_t count, const uint8_t *data) {
  printer_usb_event(event, result, count);
  static bool status_logged = false;
  static uint8_t last_status = 0;
  const printer_info_t *p = printer_host_info();
  // GET_PORT_STATUS runs once per second and flushes a short final USB block.
  if (event == PRINTER_EV_STATUS || event == PRINTER_EV_UNMOUNTED) printer_trace_flush(&tx_trace);
  if (event == PRINTER_EV_MOUNTED) {
    printer_trace_reset(&tx_trace);
    status_logged = false;
    d_printf("Printer mounted %04x:%04x address=%u interface=%u protocol=%u OUT=%02x IN=%02x\r\n",
             p->vid, p->pid, p->address, p->interface_number, p->protocol, p->ep_out, p->ep_in);
  } else if (event == PRINTER_EV_UNMOUNTED) {
    d_printf("Printer disconnected address=%u\r\n", p->address);
  } else if (event == PRINTER_EV_DEVICE_ID && p->id_valid) {
    d_printf("Printer ID%s: %s\r\n", p->id_truncated ? " (truncated)" : "", p->device_id);
  } else if (event == PRINTER_EV_STATUS && p->status_valid) {
    if (!status_logged || last_status != p->port_status) {
      d_printf("Printer status=%02x paper_empty=%u selected=%u error=%u\r\n", p->port_status,
               (p->port_status >> 5) & 1, (p->port_status >> 4) & 1, !(p->port_status & 8));
      last_status = p->port_status;
      status_logged = true;
    }
  } else if (event == PRINTER_EV_WRITE && result == XFER_RESULT_SUCCESS) {
    printer_trace_append(&tx_trace, data, count);
  } else if (result != XFER_RESULT_SUCCESS) {
    if (event == PRINTER_EV_STATUS) status_logged = false;
    d_printf("Printer event=%u result=%u transferred=%lu\r\n",
             (unsigned) event, (unsigned) result, (unsigned long)count);
  }
}
