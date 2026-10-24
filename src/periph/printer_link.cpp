#include "printer_link.h"
#include "printer.h"
#include "printer_trace.h"
#include "printer_diag.h"
#include "config.h"
#include "fpga/link.h"
#include "storage/settings.h"

namespace {
void trace_rx(uint32_t offset, const char *hex) {
  (void)offset; (void)hex; // debug macro may compile to a no-op in native tests
  d_printf("Printer RX offset=%lu hex=%s\r\n", (unsigned long)offset, hex);
}
PrinterTrace rx_trace(trace_rx);
// Leave room for DATA already queued in the FPGA/SPI path when BUSY arrives.
constexpr size_t BUSY_RESERVE = 32;
// Repeat the current level even if it did not change: a lost SPI status frame
// must not leave the FPGA waiting forever. This is NOT a DATA acknowledgement
// and never causes printer bytes to be replayed. USB port polling stays at 1 s.
constexpr uint32_t STATUS_REPEAT_MS = 100;
uint32_t last_status_sent = 0;
bool active = false, reply = false, pending = false;
uint8_t last_status = 0x80, pending_byte = 0;
uint32_t pending_session = 0, received = 0, rejected = 0, last_log = 0;

bool busy(const PrinterSnapshot &s) {
  return pending || !s.connected || s.fault || s.resetting ||
         s.available <= BUSY_RESERVE || !s.status_valid || (s.port_status & 0x38) != 0x18;
}

void flush_pending() {
  if (!pending) return;
  const auto s = printer_snapshot();
  if (s.session != pending_session) {
    ++rejected;
    pending = false;
  } else if (printer_write_if_session(&pending_byte, 1, pending_session) == 1) {
    pending = false;
  }
}

void receive(uint8_t addr, uint8_t data) {
  if (addr != 0) return;
  active = true;
  ++received;
  printer_diag_received(data);
  rx_trace.append(&data, 1); // exactly what arrived from FPGA, before enqueue
  flush_pending(); // preserve byte order if space appeared since the last tick
  if (pending) {
    ++rejected; // source continued despite BUSY; report it, never latch BUSY forever
    return;
  }
  const auto s = printer_snapshot();
  if (printer_write_if_session(&data, 1, s.session) != 1) {
    if (s.connected && !s.fault && !s.resetting) {
      pending_byte = data;
      pending_session = s.session;
      pending = true; // retain one late byte until capacity returns
    } else {
      ++rejected;
    }
  }
}

void request(uint8_t addr, uint8_t) {
  if (addr == 0) active = reply = true;
}
} // namespace

void printer_link_begin() {
  printer_diag_begin();
  link_on(CMD_PRINTER_DATA, receive);
  link_on(CMD_PRINTER_STATUS, request);
}

void printer_link_reset() {
  printer_diag_reset();
  rx_trace.flush();
  rx_trace.reset();
  active = reply = pending = false;
  received = rejected = last_log = 0;
  last_status = 0x80;
  last_status_sent = millis();
  const auto s = printer_snapshot();
  if (s.queued || s.fault) printer_reset();
}

void printer_link_tick() {
  if (!active) return;
  flush_pending();
  const auto s = printer_snapshot();
  const uint8_t status = busy(s) ? 0x80 : 0x00;
  const uint32_t now = millis();
  if (status != last_status || uint32_t(now - last_log) >= 1000) {
    last_log = now;
    d_printf("Printer link busy=%u queue=%u flight=%u free=%u pending=%u rx=%lu rejected=%lu usb=%lu connected=%u valid=%u port=%02x fault=%u reset=%u\r\n",
             unsigned(status >> 7), unsigned(s.queued), unsigned(s.in_flight),
             unsigned(s.available), unsigned(pending), (unsigned long)received,
             (unsigned long)rejected, (unsigned long)s.usb_completed,
             unsigned(s.connected), unsigned(s.status_valid), s.port_status,
             unsigned(s.fault), unsigned(s.resetting));
  }
  if (reply || status != last_status || uint32_t(now - last_status_sent) >= STATUS_REPEAT_MS) {
    rx_trace.flush(); // also expose a short final line when the source stops
    // Unsigned subtraction handles millis() wraparound. Update before spi_send:
    // SPI dispatch may re-enter the handlers and set a new reply request.
    reply = false;
    last_status = status;
    last_status_sent = now;
    spi_send(CMD_PRINTER_STATUS, 0, status);
  }
  printer_diag_tick();
}
