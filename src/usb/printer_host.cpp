#include "printer_host.h"
#include <cstring>

namespace {
enum class Control { Idle, Id, Status, Reset };
PrinterInfo info{};
Control control = Control::Idle;
uint8_t config_index[CFG_TUH_DEVICE_MAX + 1]{};
uint8_t active_config = 0;
bool id_pending = false;
uint32_t last_poll = 0;
uint16_t tx_length = 0;
tusb_desc_endpoint_t out_desc{}, in_desc{};
CFG_TUSB_MEM_ALIGN uint8_t tx_buf[PRINTER_TRANSFER_SIZE];
CFG_TUSB_MEM_ALIGN uint8_t rx_buf[PRINTER_TRANSFER_SIZE];
CFG_TUSB_MEM_ALIGN uint8_t control_buf[PRINTER_ID_SIZE];

bool init() {
  info = {};
  control = Control::Idle;
  memset(config_index, 0, sizeof(config_index));
  return true;
}

bool open(uint8_t, uint8_t address, const tusb_desc_interface_t *itf, uint16_t len) {
  if (!address || address > CFG_TUH_DEVICE_MAX || info.address ||
      len < sizeof(*itf) || itf->bLength < sizeof(*itf) || itf->bLength > len ||
      itf->bInterfaceClass != 7 || itf->bInterfaceSubClass != 1 ||
      itf->bAlternateSetting != 0 ||
      (itf->bInterfaceProtocol != 1 && itf->bInterfaceProtocol != 2)) return false;

  tusb_desc_endpoint_t out{}, in{};
  uint8_t endpoints = 0;
  const auto *bytes = reinterpret_cast<const uint8_t *>(itf);
  for (uint16_t pos = itf->bLength; pos < len;) {
    if (len - pos < 2 || bytes[pos] < 2 || bytes[pos] > len - pos) return false;
    const uint8_t size = bytes[pos], type = bytes[pos + 1];
    if (type == TUSB_DESC_INTERFACE || type == TUSB_DESC_INTERFACE_ASSOCIATION) break;
    if (type == TUSB_DESC_ENDPOINT) {
      if (size < sizeof(tusb_desc_endpoint_t)) return false;
      tusb_desc_endpoint_t ep;
      memcpy(&ep, bytes + pos, sizeof(ep));
      const auto mps = tu_edpt_packet_size(&ep);
      if (ep.bmAttributes.xfer != TUSB_XFER_BULK ||
          !(ep.bEndpointAddress & 0x0f) || (ep.bEndpointAddress & 0x70) ||
          (mps != 8 && mps != 16 && mps != 32 && mps != 64)) return false;
      auto &dest = (ep.bEndpointAddress & 0x80) ? in : out;
      if (dest.bEndpointAddress) return false;
      dest = ep;
      ++endpoints;
    }
    pos += size;
  }
  if (endpoints != itf->bNumEndpoints || !out.bEndpointAddress ||
      (itf->bInterfaceProtocol == 1 && (endpoints != 1 || in.bEndpointAddress)) ||
      (itf->bInterfaceProtocol == 2 && (endpoints != 2 || !in.bEndpointAddress))) return false;
  if (!tuh_edpt_open(address, &out)) return false;
  if (in.bEndpointAddress && !tuh_edpt_open(address, &in)) {
    tuh_edpt_close(address, out.bEndpointAddress);
    return false;
  }
  info = {};
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

bool set_config(uint8_t address, uint8_t interface_number) {
  if (address != info.address || interface_number != info.interface_number) return false;
  info.mounted = true;
  id_pending = true;
  last_poll = 0;
  printer_host_event(PrinterEvent::Mounted, XFER_RESULT_SUCCESS, 0, nullptr);
  usbh_driver_set_config_complete(address, interface_number);
  return true;
}

void close(uint8_t address) {
  if (address != info.address) return;
  // TinyUSB has cancelled device transfers before invoking class close().
  info.mounted = false;
  info.tx_busy = info.rx_busy = false;
  printer_host_event(PrinterEvent::Unmounted, XFER_RESULT_FAILED, 0, nullptr);
  info = {};
  control = Control::Idle;
  id_pending = false;
}

bool transfer_done(uint8_t address, uint8_t ep, xfer_result_t result, uint32_t length) {
  if (address != info.address) return false;
  if (ep == info.ep_out) {
    info.tx_busy = false;
    if (result == XFER_RESULT_SUCCESS && length != tx_length) result = XFER_RESULT_FAILED;
    if (result != XFER_RESULT_SUCCESS) info.fault = true;
    printer_host_event(PrinterEvent::Write, result, length, nullptr);
  } else if (ep == info.ep_in) {
    info.rx_busy = false;
    if (result != XFER_RESULT_SUCCESS) info.fault = true;
    printer_host_event(PrinterEvent::Read, result, length, rx_buf);
  } else return false;
  return true;
}

void control_done(tuh_xfer_t *xfer) {
  if (xfer->daddr != info.address || !info.mounted) return;
  const Control completed = control;
  control = Control::Idle;
  auto result = xfer->result;
  PrinterEvent event = PrinterEvent::Status;
  if (completed == Control::Id) {
    event = PrinterEvent::DeviceId;
    info.id_valid = false;
    info.id_truncated = false;
    info.device_id[0] = 0;
    if (result == XFER_RESULT_SUCCESS) {
      const uint16_t declared = (uint16_t(control_buf[0]) << 8) | control_buf[1];
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
  } else if (completed == Control::Status) {
    info.status_valid = result == XFER_RESULT_SUCCESS && xfer->actual_len == 1;
    if (info.status_valid) info.port_status = control_buf[0];
    else if (result == XFER_RESULT_SUCCESS) result = XFER_RESULT_FAILED;
  } else if (completed == Control::Reset) {
    event = PrinterEvent::Reset;
    info.status_valid = false;
    if (result == XFER_RESULT_SUCCESS) {
      // PIO-USB's hcd_edpt_clear_stall() is a no-op. Reopening idle endpoints
      // resets the host DATA toggle to match the printer's reset pipes.
      tuh_edpt_close(info.address, info.ep_out);
      if (info.ep_in) tuh_edpt_close(info.address, info.ep_in);
      bool ok = tuh_edpt_open(info.address, &out_desc);
      if (info.ep_in) ok = tuh_edpt_open(info.address, &in_desc) && ok;
      if (!ok) result = XFER_RESULT_FAILED;
    }
    info.fault = result != XFER_RESULT_SUCCESS;
  } else return;
  printer_host_event(event, result, xfer->actual_len, nullptr);
}

bool request(Control op, bool legacy = false) {
  if (!info.mounted || control != Control::Idle) return false;
  tusb_control_request_t setup{};
  setup.bmRequestType = op == Control::Reset ? (legacy ? 0x23 : 0x21) : 0xa1;
  setup.bRequest = op == Control::Id ? 0 : op == Control::Status ? 1 : 2;
  setup.wValue = tu_htole16(op == Control::Id ? active_config : 0);
  setup.wIndex = tu_htole16(op == Control::Id ? uint16_t(info.interface_number) << 8 : info.interface_number);
  setup.wLength = tu_htole16(op == Control::Id ? sizeof(control_buf) : op == Control::Status ? 1 : 0);
  tuh_xfer_t xfer{};
  xfer.daddr = info.address;
  xfer.setup = &setup; // TinyUSB copies setup on submission
  xfer.buffer = control_buf;
  xfer.complete_cb = control_done;
  control = op;
  if (tuh_control_xfer(&xfer)) return true;
  control = Control::Idle;
  return false;
}

bool bulk(uint8_t ep, uint8_t *buffer, uint16_t length) {
  if (!usbh_edpt_claim(info.address, ep)) return false;
  if (usbh_edpt_xfer(info.address, ep, buffer, length)) return true;
  usbh_edpt_release(info.address, ep);
  return false;
}
} // namespace

usbh_class_driver_t const usbh_printer_driver = {
  "Printer", init, nullptr, open, set_config, transfer_done, close
};

TU_ATTR_WEAK void printer_host_event(PrinterEvent, xfer_result_t, uint32_t, const uint8_t *) {}
const PrinterInfo &printer_host_info() { return info; }
void printer_host_config_index(uint8_t address, uint8_t index) {
  if (address && address <= CFG_TUH_DEVICE_MAX) config_index[address] = index;
}
bool printer_host_get_id() { return request(Control::Id); }
bool printer_host_get_status() { return request(Control::Status); }
bool printer_host_soft_reset(bool legacy) {
  if (info.tx_busy || info.rx_busy) return false;
  return request(Control::Reset, legacy);
}
bool printer_host_write(const uint8_t *data, uint16_t length) {
  if (!info.mounted || info.fault || info.tx_busy || control == Control::Reset ||
      !data || !length || length > sizeof(tx_buf)) return false;
  memcpy(tx_buf, data, length);
  tx_length = length;
  info.tx_busy = true;
  if (bulk(info.ep_out, tx_buf, length)) return true;
  info.tx_busy = false;
  return false;
}
bool printer_host_read() {
  if (!info.mounted || !info.ep_in || info.fault || info.rx_busy || control == Control::Reset) return false;
  info.rx_busy = true;
  if (bulk(info.ep_in, rx_buf, sizeof(rx_buf))) return true;
  info.rx_busy = false;
  return false;
}
void printer_host_tick(uint32_t now_ms) {
  if (!info.mounted || control != Control::Idle || !tuh_mounted(info.address)) return;
  if (id_pending) {
    if (printer_host_get_id()) id_pending = false;
  } else if (uint32_t(now_ms - last_poll) >= 1000) {
    // Bound attempts even when another class currently owns the control pipe.
    last_poll = now_ms;
    printer_host_get_status();
  }
}
