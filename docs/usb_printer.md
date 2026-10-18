# USB Printer Host

The initial target is Epson LX-300+II connected directly to the board's USB host
with a USB-A to USB-B cable. Hardware validation on Go confirmed enumeration,
Device ID, port status and printing `Hello from Karabas Go!` via Bulk OUT:

- VID/PID `04b8:0005`, USB 1.1, interface 0, protocol 2, OUT `0x01`, IN `0x82`.
- Device ID reports `MFG:EPSON`, model/description `LX-300+`, and command sets
  `ESCP9,PRPII9,BDC,D4` (the physical model is LX-300+II).
- Port status `0x18`: paper present, selected, no error.

The cross-task queue added in step 3 has host simulation coverage; its hardware
stress/error tests remain pending.

## Implemented (plan step 2)

`src/usb/printer_host.*` adds an application class driver to the pinned TinyUSB
stack. `src/usb/host_drivers.cpp` registers both Printer and the existing XInput
driver. The host runs in the existing USB task; no extra task or PIO program is
allocated. There is no VID/PID allowlist.

- One active printer, class 7 / subclass 1, alternate setting 0, protocol 1
  (unidirectional) or 2 (bidirectional). Protocol 3 (IEEE-1284.4), nonzero alternate
  settings and vendor-specific interfaces are not supported in this version.
- Validate endpoint descriptors before opening them. Require Bulk OUT and, for
  protocol 2, Bulk IN. Full-speed packet sizes: 8, 16, 32 or 64 bytes.
- Asynchronous raw writes of 1..512 bytes, copied into driver storage; optional
  Bulk IN reads of up to 512 bytes. Only one transfer per direction at a time.
- Automatically request IEEE-1284 Device ID once after enumeration; poll port
  status once per second. Store at most 254 ID text bytes and flag truncation.
- Report transfer errors, including STALL and short successful OUT transfers,
  and block further bulk transfers until explicit recovery. Never replay data.
- Explicit asynchronous SOFT_RESET; rejected while a bulk transfer is pending.
  After success, reopen idle endpoints to reset the host DATA toggle, since the
  installed PIO-USB HCD's clear-stall function is a no-op. A failed recovery keeps
  the driver faulted. The caller may explicitly select legacy recipient 0x23.
- On unplug, emit an unmount event and invalidate all cached state. An unmount
  supersedes any pending operation; individual completion events are not promised.

The USB host APIs and `PrinterInfo` are **USB-task-only**. They are not thread-safe
entry points for the APP task. APP now uses the service below. The temporary
automatic test string has been removed; this firmware does not print on boot.

## Printing service (plan step 3)

`src/periph/printer.h` exposes the task-safe API. `printer.cpp` owns a fixed
**256-byte queue**, including bytes awaiting USB completion. Copying to the USB
driver does not free queue space. There is no heap allocation or additional task.
Short spinlock sections protect queue/state access; USB and logging calls occur
outside the lock. Existing USB-task ticks/callbacks drive the consumer.

- `printer_write(data, length)` accepts a prefix and returns its length (0..256).
  Retain the remainder and retry on a later APP iteration. Never busy-wait for
  room. Disconnected, faulted and resetting states reject writes.
- `printer_snapshot()` returns a consistent copy: state, connection, VID/PID,
  valid port status, queued/in-flight bytes, writable capacity, session and counters.
- `printer_reset()` requests destructive recovery asynchronously. It blocks new
  writes immediately, waits for an already reserved/in-flight USB block, discards
  the queue, then issues SOFT_RESET. Check `resetting` and `fault` in snapshots for
  completion. A disconnected reset clears local state only. It cannot retract
  bytes already accepted by the physical printer.

Known paper-out, deselected or error status pauses further USB submissions; APP
may still fill the bounded queue. Unknown status permits raw printing (some
devices do not support reliable status queries). USB failures freeze the queue
and reject further writes until explicit reset. No data is automatically replayed.
Disconnect discards queued data and records an interrupted job as a sticky fault;
reconnection alone does not resume it. A reset may remain pending indefinitely
while a device NAKs an in-flight transfer; no unsafe cancellation is attempted.

Counters are cumulative uint32 values (wrap on overflow): `accepted` is bytes
queued, `usb_completed` is bytes confirmed by full successful USB writes,
`discarded` is bytes removed without such confirmation, and `uncertain` counts
in-flight bytes affected by a failed transfer/disconnect. `uncertain` overlaps
discarded bytes after recovery; it is not an additional delivery count.
An empty queue is not proof of physical printing. `session` changes on mount,
disconnect and reset request, for future producer/protocol integration.

All writes/reset operations must go through the service once integrated. Direct
`printer_host_write()` calls bypass its completion accounting. Raw driver access
remains for tests and USB-task internals. Bulk IN is not consumed by the service.
The FPGA protocol, core-switch hooks, OSD controls and producer-side retry logic
are still subsequent steps; there is no FPGA data source wired to this queue yet.

## API contract

See `src/usb/printer_host.h`. Submission returns `false` if busy, unavailable,
faulted or given invalid input; it does not imply a completion callback will follow.
Successful submission returns immediately; completion is delivered through
`printer_host_event()`. Raw read data is valid only during that callback.

`printer_host_get_id()` and `printer_host_get_status()` share the single control
request buffer with reset. A failed/short status request invalidates cached status;
an unsupported ID/status request does not prevent raw printing. USB write success
means delivery to the device, not completion of physical printing.

NAK is left to the USB controller to retry; a printer waiting for paper may keep
a transfer pending indefinitely. There is no driver-level cancellation or timeout
yet. Disconnecting releases the state. Reset cannot currently cancel a pending
transfer. Do not use automatic timeouts to replay bytes of an uncertain transfer.

## Diagnostics and hardware check

Enable `debug=1` in `karabas.ini`. On connection, the debug log reports VID/PID,
interface, protocol, endpoint addresses, Device ID (if available), and changes to
port status. Status bits: 5 = paper empty, 4 = selected, 3 = no error. Some adapters
may return fixed status values.

When the printer is available:

1. Connect it to Karabas Go's host port, directly and then through the intended hub.
2. Capture the mount/ID/status log; if it does not bind, capture full descriptors
   on a PC to check alternate settings and protocol.
3. Exercise text and ESC/P data using the APP service API; respect partial acceptance.
4. Check paper removal, offline, disconnect during transfer, reconnection, and
   explicit reset after failure. Verify USB keyboard/mouse/XInput remain usable.

The OSD test page and printing from FPGA cores are not implemented at this stage.

## Validation

- Firmware: `pio run -e go` (only GO is requested for this change).
- Host simulation: `python test/usb_printer_host/run.py` with a C++17 `g++` on PATH
  (or set `CXX` to a compiler executable). This compiles the actual driver against
  a fake TinyUSB host in a temporary directory. It checks malformed descriptors,
  protocol selection, endpoint rollback, request fields, ID bounds, status
  validity, asynchronous buffer ownership, backpressure, failed submissions,
  partial writes, STALL, reset recovery, unplug and unidirectional devices.
- The same runner tests the actual service plus driver, using a host mutex in
  place of the RP2040 spinlock: 256-byte bound including in-flight data, wraparound,
  pause/resume, submission failures, explicit reset, interruption/reconnection,
  and a concurrent 50,000-byte producer/consumer stream with exact byte comparison.

Simulation does not validate PIO timing, electrical connectivity or printer behavior.

## References

- [USB Printer Class 1.1](https://www.usb.org/sites/default/files/usbprint11a021811.pdf)
- [Epson LX-300+II product information](https://files.support.epson.com/pdf/lx3pii/lx3piipg.pdf)
