#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t PRINTER_QUEUE_SIZE = 2048;
enum class PrinterState { Disconnected, Idle, Sending, Paused, Fault, Resetting };

struct PrinterSnapshot {
  PrinterState state;
  bool connected, status_valid, fault, resetting;
  uint8_t port_status;
  uint16_t vid, pid;
  size_t queued;   // includes in-flight bytes, released only on full USB success
  size_t in_flight;
  size_t available; // writable bytes now; zero if disconnected/faulted/resetting
  uint32_t session; // changes on mount, disconnect and explicit reset request
  uint32_t accepted, usb_completed, discarded, uncertain;
};

// Task-safe APP API. No USB calls, waits for buffer space or dynamic allocation.
// Returns accepted prefix length (0..2048). Caller must retain/retry the remainder.
size_t printer_write(const uint8_t *data, size_t length);
// Reject data if disconnect/reset changed the session since the producer read it.
size_t printer_write_if_session(const uint8_t *data, size_t length, uint32_t session);
PrinterSnapshot printer_snapshot();
// Explicit destructive recovery: stop accepting data, wait for any in-flight
// block, discard the queue, then request USB SOFT_RESET. Never replays data.
// true = request accepted, not reset completed. Inspect snapshot for completion.
// While disconnected, this clears local state without a USB request.
bool printer_reset();
