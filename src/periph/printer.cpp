#include "printer.h"
#include "printer_usb.h"
#include "hardware/sync.h"
#include <cstring>

namespace {
static_assert(PRINTER_QUEUE_SIZE >= PRINTER_TRANSFER_SIZE, "Queue must hold a USB block");
spin_lock_t *lock = spin_lock_init(spin_lock_claim_unused(true));
struct Guard {
  uint32_t saved = spin_lock_blocking(lock);
  ~Guard() { spin_unlock(lock, saved); }
};
uint8_t queue[PRINTER_QUEUE_SIZE];
// Staging is USB-task-owned; never hold the spinlock across a USB operation.
uint8_t staging[PRINTER_TRANSFER_SIZE];
size_t head = 0, used = 0, in_flight = 0;
bool connected = false, fault = false, resetting = false, reset_submitted = false;
bool status_valid = false;
uint8_t port_status = 0;
uint16_t vid = 0, pid = 0;
uint32_t session = 0, accepted = 0, completed = 0, discarded = 0, uncertain = 0;

bool writable() { return connected && !fault && !resetting; }
bool paused() { return status_valid && (port_status & 0x38) != 0x18; }
void discard() {
  discarded += used;
  head = used = 0;
}
} // namespace

static size_t write_bytes(const uint8_t *data, size_t length, const uint32_t *expected_session) {
  if (!data || !length) return 0;
  Guard guard;
  if (!writable()) return 0;
  if (expected_session && *expected_session != session) return 0;
  size_t n = PRINTER_QUEUE_SIZE - used;
  if (n > length) n = length;
  const size_t tail = (head + used) % PRINTER_QUEUE_SIZE;
  size_t first = PRINTER_QUEUE_SIZE - tail;
  if (first > n) first = n;
  memcpy(queue + tail, data, first);
  memcpy(queue, data + first, n - first);
  used += n;
  accepted += n;
  return n;
}

size_t printer_write(const uint8_t *data, size_t length) {
  return write_bytes(data, length, nullptr);
}

size_t printer_write_if_session(const uint8_t *data, size_t length, uint32_t expected_session) {
  return write_bytes(data, length, &expected_session);
}

PrinterSnapshot printer_snapshot() {
  Guard guard;
  PrinterState state = PrinterState::Idle;
  if (resetting) state = PrinterState::Resetting;
  else if (!connected) state = PrinterState::Disconnected;
  else if (fault) state = PrinterState::Fault;
  else if (in_flight) state = PrinterState::Sending;
  else if (paused()) state = PrinterState::Paused;
  return {state, connected, status_valid, fault, resetting, port_status, vid, pid,
          used, in_flight, writable() ? PRINTER_QUEUE_SIZE - used : 0,
          session, accepted, completed, discarded, uncertain};
}

bool printer_reset() {
  Guard guard;
  if (resetting) return false;
  resetting = true;
  ++session;
  return true;
}

void printer_usb_event(PrinterEvent event, xfer_result_t result, uint32_t count) {
  const auto &info = printer_host_info(); // same USB task as the driver
  Guard guard;
  status_valid = info.status_valid;
  port_status = info.port_status;
  if (event == PrinterEvent::Mounted) {
    // Reconnection starts a new empty job; never replay the interrupted stream.
    discard();
    in_flight = 0;
    fault = resetting = reset_submitted = status_valid = false;
    port_status = 0;
    connected = true;
    vid = info.vid;
    pid = info.pid;
    ++session;
  } else if (event == PrinterEvent::Unmounted) {
    fault = fault || used != 0;
    uncertain += in_flight;
    discard();
    in_flight = 0;
    connected = status_valid = resetting = reset_submitted = false;
    port_status = 0;
    vid = pid = 0;
    ++session;
  } else if (event == PrinterEvent::Write && in_flight) {
    if (result == XFER_RESULT_SUCCESS && count == in_flight) {
      head = (head + in_flight) % PRINTER_QUEUE_SIZE;
      used -= in_flight;
      completed += in_flight;
    } else {
      fault = true;
      uncertain += in_flight;
    }
    in_flight = 0;
  } else if (event == PrinterEvent::Reset && reset_submitted) {
    fault = result != XFER_RESULT_SUCCESS;
    resetting = reset_submitted = false;
  }
  if (info.fault && connected) fault = true;
}

void printer_usb_tick() {
  bool do_reset = false;
  size_t length = 0;
  {
    Guard guard;
    if (resetting) {
      if (in_flight || reset_submitted) return;
      discard();
      if (!connected) {
        fault = resetting = false;
        return;
      }
      reset_submitted = true;
      do_reset = true;
    } else {
      if (!writable() || paused() || in_flight || !used) return;
      length = used;
      if (length > sizeof(staging)) length = sizeof(staging);
      size_t first = PRINTER_QUEUE_SIZE - head;
      if (first > length) first = length;
      memcpy(staging, queue + head, first);
      memcpy(staging + first, queue, length - first);
      // This reservation precedes any concurrent APP reset request. Such a reset
      // waits for this one block; it prevents all subsequent submissions.
      in_flight = length;
    }
  }
  if (do_reset) {
    if (!printer_host_soft_reset()) {
      Guard guard;
      reset_submitted = false; // control pipe busy; retry submission later
    }
  } else if (!printer_host_write(staging, length)) {
    Guard guard;
    in_flight = 0; // nothing submitted; keep the queued bytes for a later tick
  }
}
