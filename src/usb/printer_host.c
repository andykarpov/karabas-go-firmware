#include "printer_host.h"
#include <string.h>
#include "ms_time.h"
#include "pio_usb_ll.h"

// TinyUSB 0.18 has no tuh_edpt_close() (Adafruit's had): close it in PIO-USB, the next
// tuh_edpt_open() configures it again with DATA0 (PIO-USB's root port 0 is TinyUSB's rhport 1)
static void edpt_close(uint8_t address, uint8_t ep) {
  tuh_edpt_abort_xfer(address, ep);
  pio_usb_host_endpoint_close(BOARD_TUH_RHPORT - 1, address, ep);
}

typedef enum { CONTROL_IDLE, CONTROL_ID, CONTROL_STATUS, CONTROL_RESET } control_t;
static printer_info_t info;
static control_t control = CONTROL_IDLE;
static uint8_t config_index[CFG_TUH_DEVICE_MAX + 1];
static uint8_t active_config = 0;
static bool id_pending = false;
static uint32_t last_poll = 0;
static uint16_t tx_length = 0;
static tusb_desc_endpoint_t out_desc, in_desc;
CFG_TUSB_MEM_ALIGN static uint8_t tx_buf[PRINTER_TRANSFER_SIZE];
CFG_TUSB_MEM_ALIGN static uint8_t rx_buf[PRINTER_TRANSFER_SIZE];
CFG_TUSB_MEM_ALIGN static uint8_t control_buf[PRINTER_ID_SIZE];

static bool init(void) {
  memset(&info, 0, sizeof(info));
  control = CONTROL_IDLE;
  memset(config_index, 0, sizeof(config_index));
  return true;
}

static bool open(uint8_t rhport, uint8_t address, const tusb_desc_interface_t *itf, uint16_t len) {
  (void) rhport;
  if (!address || address > CFG_TUH_DEVICE_MAX || info.address ||
      len < sizeof(*itf) || itf->bLength < sizeof(*itf) || itf->bLength > len ||
      itf->bInterfaceClass != 7 || itf->bInterfaceSubClass != 1 ||
      itf->bAlternateSetting != 0 ||
      (itf->bInterfaceProtocol != 1 && itf->bInterfaceProtocol != 2)) return false;

  tusb_desc_endpoint_t out = {0}, in = {0};
  uint8_t endpoints = 0;
  const uint8_t *bytes = (const uint8_t *) itf;
  for (uint16_t pos = itf->bLength; pos < len;) {
    if (len - pos < 2 || bytes[pos] < 2 || bytes[pos] > len - pos) return false;
    const uint8_t size = bytes[pos], type = bytes[pos + 1];
    if (type == TUSB_DESC_INTERFACE || type == TUSB_DESC_INTERFACE_ASSOCIATION) break;
    if (type == TUSB_DESC_ENDPOINT) {
      if (size < sizeof(tusb_desc_endpoint_t)) return false;
      tusb_desc_endpoint_t ep;
      memcpy(&ep, bytes + pos, sizeof(ep));
      const uint16_t mps = tu_edpt_packet_size(&ep);
      if (ep.bmAttributes.xfer != TUSB_XFER_BULK ||
          !(ep.bEndpointAddress & 0x0f) || (ep.bEndpointAddress & 0x70) ||
          (mps != 8 && mps != 16 && mps != 32 && mps != 64)) return false;
      tusb_desc_endpoint_t *dest = (ep.bEndpointAddress & 0x80) ? &in : &out;
      if (dest->bEndpointAddress) return false;
      *dest = ep;
      ++endpoints;
    }
    pos += size;
  }
  if (endpoints != itf->bNumEndpoints || !out.bEndpointAddress ||
      (itf->bInterfaceProtocol == 1 && (endpoints != 1 || in.bEndpointAddress)) ||
      (itf->bInterfaceProtocol == 2 && (endpoints != 2 || !in.bEndpointAddress))) return false;
  if (!tuh_edpt_open(address, &out)) return false;
  if (in.bEndpointAddress && !tuh_edpt_open(address, &in)) {
    edpt_close(address, out.bEndpointAddress);
    return false;
  }
  memset(&info, 0, sizeof(info));
  info.address = address;
  info.interface_number = itf->bInterfaceNumber;
  info.protocol = itf->bInterfaceProtocol;
  info.ep_out = out.bEndpointAddress;
  info.ep_in = in.bEndpointAddress;
  tuh_vid_pid_get(address, &info.vid, &info.pid);
  out_desc = out;
  in_desc = in;
  active_config = config_index[address];
  return true;
}

static bool set_config(uint8_t address, uint8_t interface_number) {
  if (address != info.address || interface_number != info.interface_number) return false;
  info.mounted = true;
  id_pending = true;
  last_poll = 0;
  printer_host_event(PRINTER_EV_MOUNTED, XFER_RESULT_SUCCESS, 0, NULL);
  usbh_driver_set_config_complete(address, interface_number);
  return true;
}

static void close(uint8_t address) {
  if (address != info.address) return;
  // TinyUSB has cancelled device transfers before invoking class close().
  info.mounted = false;
  info.tx_busy = info.rx_busy = false;
  printer_host_event(PRINTER_EV_UNMOUNTED, XFER_RESULT_FAILED, 0, NULL);
  memset(&info, 0, sizeof(info));
  control = CONTROL_IDLE;
  id_pending = false;
}

static bool transfer_done(uint8_t address, uint8_t ep, xfer_result_t result, uint32_t length) {
  if (address != info.address) return false;
  if (ep == info.ep_out) {
    info.tx_busy = false;
    if (result == XFER_RESULT_SUCCESS && length != tx_length) result = XFER_RESULT_FAILED;
    if (result != XFER_RESULT_SUCCESS) info.fault = true;
    // The buffer remains owned by this USB task throughout the callback.
    // Expose successful OUT data for byte-exact diagnostics, not just counts.
    printer_host_event(PRINTER_EV_WRITE, result, length, tx_buf);
  } else if (ep == info.ep_in) {
    info.rx_busy = false;
    if (result != XFER_RESULT_SUCCESS) info.fault = true;
    printer_host_event(PRINTER_EV_READ, result, length, rx_buf);
  } else return false;
  return true;
}

static void control_done(tuh_xfer_t *xfer) {
  if (xfer->daddr != info.address || !info.mounted) return;
  const control_t completed = control;
  control = CONTROL_IDLE;
  xfer_result_t result = xfer->result;
  printer_event_t event = PRINTER_EV_STATUS;
  if (completed == CONTROL_ID) {
    event = PRINTER_EV_DEVICE_ID;
    info.id_valid = false;
    info.id_truncated = false;
    info.device_id[0] = 0;
    if (result == XFER_RESULT_SUCCESS) {
      const uint16_t declared = ((uint16_t) control_buf[0] << 8) | control_buf[1];
      if (xfer->actual_len < 2 || declared < 2 ||
          (declared > xfer->actual_len && xfer->actual_len < sizeof(control_buf))) {
        result = XFER_RESULT_FAILED;
      } else {
        uint32_t size = declared < xfer->actual_len ? declared : xfer->actual_len;
        if (size > sizeof(control_buf)) size = sizeof(control_buf);
        memcpy(info.device_id, control_buf + 2, size - 2);
        info.device_id[size - 2] = 0;
        info.id_valid = true;
        info.id_truncated = declared > size;
      }
    }
  } else if (completed == CONTROL_STATUS) {
    info.status_valid = result == XFER_RESULT_SUCCESS && xfer->actual_len == 1;
    if (info.status_valid) info.port_status = control_buf[0];
    else if (result == XFER_RESULT_SUCCESS) result = XFER_RESULT_FAILED;
  } else if (completed == CONTROL_RESET) {
    event = PRINTER_EV_RESET;
    info.status_valid = false;
    if (result == XFER_RESULT_SUCCESS) {
      // PIO-USB's hcd_edpt_clear_stall() is a no-op. Reopening idle endpoints
      // resets the host DATA toggle to match the printer's reset pipes.
      edpt_close(info.address, info.ep_out);
      if (info.ep_in) edpt_close(info.address, info.ep_in);
      bool ok = tuh_edpt_open(info.address, &out_desc);
      if (info.ep_in) ok = tuh_edpt_open(info.address, &in_desc) && ok;
      if (!ok) result = XFER_RESULT_FAILED;
    }
    info.fault = result != XFER_RESULT_SUCCESS;
  } else return;
  printer_host_event(event, result, xfer->actual_len, NULL);
}

static bool request(control_t op, bool legacy) {
  if (!info.mounted || control != CONTROL_IDLE) return false;
  tusb_control_request_t setup = {0};
  setup.bmRequestType = op == CONTROL_RESET ? (legacy ? 0x23 : 0x21) : 0xa1;
  setup.bRequest = op == CONTROL_ID ? 0 : op == CONTROL_STATUS ? 1 : 2;
  setup.wValue = tu_htole16(op == CONTROL_ID ? active_config : 0);
  setup.wIndex = tu_htole16(op == CONTROL_ID ? (uint16_t) info.interface_number << 8 : info.interface_number);
  setup.wLength = tu_htole16(op == CONTROL_ID ? sizeof(control_buf) : op == CONTROL_STATUS ? 1 : 0);
  tuh_xfer_t xfer = {0};
  xfer.daddr = info.address;
  xfer.setup = &setup; // TinyUSB copies setup on submission
  xfer.buffer = control_buf;
  xfer.complete_cb = control_done;
  control = op;
  if (tuh_control_xfer(&xfer)) return true;
  control = CONTROL_IDLE;
  return false;
}

static bool bulk(uint8_t ep, uint8_t *buffer, uint16_t length) {
  if (!usbh_edpt_claim(info.address, ep)) return false;
  if (usbh_edpt_xfer(info.address, ep, buffer, length)) return true;
  usbh_edpt_release(info.address, ep);
  return false;
}

usbh_class_driver_t const usbh_printer_driver = {
  "Printer", init, NULL, open, set_config, transfer_done, close
};

TU_ATTR_WEAK void printer_host_event(printer_event_t event, xfer_result_t result, uint32_t transferred, const uint8_t *data) {
  (void) event; (void) result; (void) transferred; (void) data;
}
const printer_info_t *printer_host_info(void) { return &info; }
void printer_host_config_index(uint8_t address, uint8_t index) {
  if (address && address <= CFG_TUH_DEVICE_MAX) config_index[address] = index;
}
bool printer_host_get_id(void) { return request(CONTROL_ID, false); }
bool printer_host_get_status(void) { return request(CONTROL_STATUS, false); }
bool printer_host_soft_reset(bool legacy) {
  if (info.tx_busy || info.rx_busy) return false;
  return request(CONTROL_RESET, legacy);
}
bool printer_host_write(const uint8_t *data, uint16_t length) {
  if (!info.mounted || info.fault || info.tx_busy || control == CONTROL_RESET ||
      !data || !length || length > sizeof(tx_buf)) return false;
  memcpy(tx_buf, data, length);
  tx_length = length;
  info.tx_busy = true;
  if (bulk(info.ep_out, tx_buf, length)) return true;
  info.tx_busy = false;
  return false;
}
bool printer_host_read(void) {
  if (!info.mounted || !info.ep_in || info.fault || info.rx_busy || control == CONTROL_RESET) return false;
  info.rx_busy = true;
  if (bulk(info.ep_in, rx_buf, sizeof(rx_buf))) return true;
  info.rx_busy = false;
  return false;
}
// the USB loop runs from RAM; the time is read only with a printer
void __not_in_flash_func(printer_host_tick)(void) {
  if (!info.mounted || control != CONTROL_IDLE || !tuh_mounted(info.address)) return;
  uint32_t now_ms = ms_now();
  if (id_pending) {
    if (printer_host_get_id()) id_pending = false;
  } else if ((uint32_t) (now_ms - last_poll) >= 1000) {
    // Bound attempts even when another class currently owns the control pipe.
    last_poll = now_ms;
    printer_host_get_status();
  }
}
