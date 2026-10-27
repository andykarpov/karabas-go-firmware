#include "printer.h"
#include "printer_usb.h"
#include "hardware/sync.h"
#include <string.h>

_Static_assert(PRINTER_QUEUE_SIZE >= PRINTER_TRANSFER_SIZE, "Queue must hold a USB block");
static spin_lock_t *lock;
static uint8_t queue[PRINTER_QUEUE_SIZE];
// Staging is USB-task-owned; never hold the spinlock across a USB operation.
static uint8_t staging[PRINTER_TRANSFER_SIZE];
static size_t head = 0, used = 0, in_flight = 0;
static bool connected = false, fault = false, resetting = false, reset_submitted = false;
static bool status_valid = false;
static uint8_t port_status = 0;
static uint16_t vid = 0, pid = 0;
static uint32_t session = 0, accepted = 0, completed = 0, discarded = 0, uncertain = 0;

// before main(), as the C++ static initializer it was
__attribute__((constructor)) static void printer_lock_init(void) {
  lock = spin_lock_init(spin_lock_claim_unused(true));
}

static __force_inline bool writable(void) { return connected && !fault && !resetting; }
static __force_inline bool paused(void) { return status_valid && (port_status & 0x38) != 0x18; }
static void discard(void) {
  discarded += used;
  head = used = 0;
}

static size_t write_bytes(const uint8_t *data, size_t length, const uint32_t *expected_session) {
  if (!data || !length) return 0;
  uint32_t saved = spin_lock_blocking(lock);
  if (!writable() || (expected_session && *expected_session != session)) {
    spin_unlock(lock, saved);
    return 0;
  }
  size_t n = PRINTER_QUEUE_SIZE - used;
  if (n > length) n = length;
  const size_t tail = (head + used) % PRINTER_QUEUE_SIZE;
  size_t first = PRINTER_QUEUE_SIZE - tail;
  if (first > n) first = n;
  memcpy(queue + tail, data, first);
  memcpy(queue, data + first, n - first);
  used += n;
  accepted += n;
  spin_unlock(lock, saved);
  return n;
}

size_t printer_write(const uint8_t *data, size_t length) {
  return write_bytes(data, length, NULL);
}

size_t printer_write_if_session(const uint8_t *data, size_t length, uint32_t expected_session) {
  return write_bytes(data, length, &expected_session);
}

printer_snapshot_t printer_snapshot(void) {
  uint32_t saved = spin_lock_blocking(lock);
  printer_state_t state = PRINTER_IDLE;
  if (resetting) state = PRINTER_RESETTING;
  else if (!connected) state = PRINTER_DISCONNECTED;
  else if (fault) state = PRINTER_FAULT;
  else if (in_flight) state = PRINTER_SENDING;
  else if (paused()) state = PRINTER_PAUSED;
  printer_snapshot_t snap = {state, connected, status_valid, fault, resetting, port_status, vid, pid,
          used, in_flight, writable() ? PRINTER_QUEUE_SIZE - used : 0,
          session, accepted, completed, discarded, uncertain};
  spin_unlock(lock, saved);
  return snap;
}

bool printer_reset(void) {
  uint32_t saved = spin_lock_blocking(lock);
  bool ok = !resetting;
  if (ok) {
    resetting = true;
    ++session;
  }
  spin_unlock(lock, saved);
  return ok;
}

void printer_usb_event(printer_event_t event, xfer_result_t result, uint32_t count) {
  const printer_info_t *info = printer_host_info(); // same USB task as the driver
  uint32_t saved = spin_lock_blocking(lock);
  status_valid = info->status_valid;
  port_status = info->port_status;
  if (event == PRINTER_EV_MOUNTED) {
    // Reconnection starts a new empty job; never replay the interrupted stream.
    discard();
    in_flight = 0;
    fault = resetting = reset_submitted = status_valid = false;
    port_status = 0;
    connected = true;
    vid = info->vid;
    pid = info->pid;
    ++session;
  } else if (event == PRINTER_EV_UNMOUNTED) {
    fault = fault || used != 0;
    uncertain += in_flight;
    discard();
    in_flight = 0;
    connected = status_valid = resetting = reset_submitted = false;
    port_status = 0;
    vid = pid = 0;
    ++session;
  } else if (event == PRINTER_EV_WRITE && in_flight) {
    if (result == XFER_RESULT_SUCCESS && count == in_flight) {
      head = (head + in_flight) % PRINTER_QUEUE_SIZE;
      used -= in_flight;
      completed += in_flight;
    } else {
      fault = true;
      uncertain += in_flight;
    }
    in_flight = 0;
  } else if (event == PRINTER_EV_RESET && reset_submitted) {
    fault = result != XFER_RESULT_SUCCESS;
    resetting = reset_submitted = false;
  }
  if (info->fault && connected) fault = true;
  spin_unlock(lock, saved);
}

void __not_in_flash_func(printer_usb_tick)(void) { // the USB loop runs from RAM
  bool do_reset = false;
  size_t length = 0;
  uint32_t saved = spin_lock_blocking(lock);
  if (resetting) {
    if (in_flight || reset_submitted) { spin_unlock(lock, saved); return; }
    discard();
    if (!connected) {
      fault = resetting = false;
      spin_unlock(lock, saved);
      return;
    }
    reset_submitted = true;
    do_reset = true;
  } else {
    if (!writable() || paused() || in_flight || !used) { spin_unlock(lock, saved); return; }
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
  spin_unlock(lock, saved);
  if (do_reset) {
    if (!printer_host_soft_reset(false)) {
      saved = spin_lock_blocking(lock);
      reset_submitted = false; // control pipe busy; retry submission later
      spin_unlock(lock, saved);
    }
  } else if (!printer_host_write(staging, length)) {
    saved = spin_lock_blocking(lock);
    in_flight = 0; // nothing submitted; keep the queued bytes for a later tick
    spin_unlock(lock, saved);
  }
}
