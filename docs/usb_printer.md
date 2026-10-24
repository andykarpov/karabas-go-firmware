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
**2048-byte queue**, including bytes awaiting USB completion. Copying to the USB
driver does not free queue space. There is no heap allocation or additional task.
Short spinlock sections protect queue/state access; USB and logging calls occur
outside the lock. Existing USB-task ticks/callbacks drive the consumer.

- `printer_write(data, length)` accepts a prefix and returns its length (0..2048).
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
Disconnect discards queued data and records an interrupted job as a fault while
disconnected. Reconnection clears that fault and any pending reset, starting with
an empty queue and unknown status; old data is never replayed. SPI BUSY remains
set until a fresh ready status is received. Diagnostic counters are retained.
A reset may remain pending indefinitely
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
The MCU SPI adapter (`src/periph/printer_link.cpp`) accepts raw bytes with command
`0x13` and reports BUSY in bit 7 using command `0x14`. It includes a core-switch
hook and an atomic session-checked enqueue. See the USB printer sequence in
[protocol.md](protocol.md) / [protocol.ru.md](protocol.ru.md) for frame fields,
status-only flow control. There are no per-byte acknowledgements or SPI reset
commands. OSD controls remain to be added.

Once the core has used a printer command, the MCU repeats `14 00 ss` every
100 ms, including unchanged READY/BUSY. This repairs a lost status update;
it does not acknowledge individual bytes or change the 1 s USB status poll.
The GO PROFI MCU HDL also retries an unanswered status query after 2,800,000
CLK cycles (100 ms at 28 MHz, about 117 ms at 24 MHz). CPU reads share an
outstanding query. A timeout schedules only a status query through the normal
FIFO arbiter: it never clears BUSY or retransmits DATA. A received status is
still required to release the post-DATA wait. Update both firmware and FPGA
core to use both recovery mechanisms.

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

The local PIO-USB host retries a bulk OUT packet after the first two invalid or
missing handshakes, on subsequent USB frames. Its buffer and DATA0/DATA1 toggle
stay unchanged until ACK; this also handles a packet accepted by the printer
whose ACK was lost, without printing it twice. ACK or NAK clears the consecutive
error count. STALL or a third consecutive error still stops the transfer and
latches the service fault. This packet-level policy applies to bulk OUT endpoints
on the PIO host; it does not replay an application buffer or reset the printer.

SPI replies from all tasks are enqueued under the SPI bus mutex, then dispatched
by APP in wire order. Releasing that mutex before publishing a reply previously
allowed a later DATA/status frame to overtake it during a task switch.

## Diagnostics and hardware check

Enable `debug=1` in `karabas.ini`. On connection, the debug log reports VID/PID,
interface, protocol, endpoint addresses, Device ID (if available), and changes to
port status. Status bits: 5 = paper empty, 4 = selected, 3 = no error. Some adapters
may return fixed status values.

After SPI printer traffic starts, `Printer link` diagnostics appear on BUSY
transitions and once per second: `queue`, `flight`, `free`, deferred `pending`,
received `rx`, `rejected`, successfully delivered `usb`, connection, status validity,
raw `port` status, service `fault` and `reset`. This distinguishes queue pressure
from stalled USB transfers and printer errors. BUSY asserts with 32 free bytes
remaining and clears as capacity returns; a single late byte is retained outside
the 2048-byte queue. Further overruns are counted, not silently latched as BUSY.
`free=0` with `fault=1` means writes are disabled, even if `queue` is only one
byte. A ready `port=18` does not clear a failed bulk transfer. Preserve the earlier
`Printer event=4 result=... transferred=...` line to distinguish failure from STALL.

With `debug=1`, byte traces replace the per-transfer success messages:
`Printer RX offset=N hex=...` contains bytes received from FPGA, and
`Printer TX offset=N hex=...` contains bytes of successfully completed USB OUT
transfers. Each line holds at most 32 bytes; CR/LF appear as `0D 0A`, without text
conversion. RX flushes on a status transmission and TX on USB status polling,
so wait at least two seconds after printing for final partial lines. RX offsets
restart on core reload, TX offsets on printer mount; gaps in offsets can mean
lost debug output and must not be mistaken for lost printer bytes.

For the supplied BASIC test that sends decimal 48 through 100 followed by 10, 13,
each cycle contains 55 bytes: hex `30 ... 64 0A 0D`. Check RX against this
sequence first, then compare its contents with TX. A wrong RX sequence locates
the fault before the printer service; equal correct RX/TX traces require looking
at device-side processing or USB delivery, not just comparing byte counters.

### FPGA / SPI loss localisation

The counters, CRC logic and command `15` were removed from GO HDL after the
SPI fix passed repeated hardware printing tests. Current GO ignores diagnostic
requests, so it no longer produces `Printer DIAG` reports. The following snapshot
instructions apply only to the earlier instrumented FPGA builds. RP2040 debug
support is unchanged.

Matching FPGA HDL adds a read-only diagnostic command `15`. With `debug=1`,
RP2040 requests a snapshot after at least 500 ms without printer DATA, at most
once every two seconds. A tagged, ordered, CRC-protected reply produces:

`Printer DIAG settled=1 z80=5555 zcrc=d44a871d rejected=0 fifo=5555 wire=5555 wcrc=d44a871d rx=5555 rcrc=d44a871d`

- `z80`, `zcrc`: all OUT strobes and their data at the MCU VHDL printer input.
- `rejected`: OUT strobes rejected because the local byte/status latch was busy.
- `fifo`: printer DATA frames actually accepted by the outgoing FPGA FIFO.
- `wire`, `wcrc`: complete DATA frames observed on internal MISO at SPI rising edges.
  Partial frames are not counted; this observer cannot detect electrical corruption
  between the FPGA output and RP2040 input.
- `rx`, `rcrc`: DATA bytes dispatched by the RP2040 SPI link.
- `settled=1`: two successive reports, including RP2040 counters, were identical.
  Compare only after the BASIC program has stopped and the queue has drained.

CRC is CRC-32/ISO-HDLC, over data bytes only. The exact BASIC program with
`FOR j=0 TO 100` produces 5555 bytes and CRC `d44a871d`; `j=0 TO 19` produces
1100 bytes and CRC `180475cb`. LF then CR is significant for the CRC.

Update both firmware and FPGA, enable debug, then reload the core or power-cycle
before running the test once. FPGA diagnostics reset on `N_RESET`; RP2040
diagnostics reset on core load. Resetting only one side makes cumulative values
incomparable. Wait at least five seconds after STOP and save a settled report.
If input CRC/count already differ, inspect CPU/port capture. Rejections or a
smaller FIFO count locate loss in FPGA acceptance. FIFO/wire differences locate
loss before completed SPI output. Wire/RX differences locate loss in SPI reception
or dispatch. Equal correct values move investigation to RX/TX and USB delivery.
These are diagnostics, not a fix or a delivery acknowledgement.

The GO HDL transmitter now holds two outgoing words in registers until each
complete 24-bit SPI frame finishes. Per-slot ownership toggles cross CLK/SCK
through two-flop synchronizers. The shared FIFO is read only to fill a free slot;
the old `spi_slave.di_req_o` speculative prefetch no longer advances it.
A partial frame retains its word for the next transaction. If a word is not yet
visible in the SCK domain, the transmitter sends `FF 00 00` (NOP) directly;
synthetic NOPs are no longer inserted into the FIFO. Two slots allow consecutive
full-rate frames while the preceding acknowledgement crosses back to CLK.
The master must keep CS asserted through the final falling SCK edge (SPI mode 0).
These acknowledgements are internal to FPGA; commands `13` and `14` are unchanged.

The regression `tb_mcu_printer_spi_timing.vhd` exercises a late aborted DATA
frame, short CS gaps, variable byte gaps and clocks for another SPI slave.
The previous transmitter lost a byte in the abort test; the new one preserves
the sequence. Current tests also verify that the removed command `15` produces
no replies or extra DATA. This demonstrates a transport vulnerability,
but does not establish that aborted frames caused the observed hardware loss.
Hardware validation of the fix delivered all 5555 BASIC-test bytes with matching
Z80/SPI/RP2040 CRC `d44a871d`; the user also confirmed repeated prolonged printing.

When the printer is available:

1. Connect it to Karabas Go's host port, directly and then through the intended hub.
2. Capture the mount/ID/status log; if it does not bind, capture full descriptors
   on a PC to check alternate settings and protocol.
3. Exercise text and ESC/P data using the APP service API; respect partial acceptance.
4. Check paper removal, offline, disconnect during transfer, reconnection, and
   explicit reset after failure. Verify USB keyboard/mouse/XInput remain usable.

The OSD test page is not implemented. Printing from a core requires matching HDL.

## Validation

- Firmware: `pio run -e go` (only GO is requested for this change).
- Host simulation: `python test/usb_printer_host/run.py` with a C++17 `g++` on PATH
  (or set `CXX` to a compiler executable). This compiles the actual driver against
  a fake TinyUSB host in a temporary directory. It checks malformed descriptors,
  protocol selection, endpoint rollback, request fields, ID bounds, status
  validity, asynchronous buffer ownership, backpressure, failed submissions,
  partial writes, STALL, reset recovery, unplug and unidirectional devices.
- The same runner tests the actual service plus driver, using a host mutex in
  place of the RP2040 spinlock: 2048-byte bound including in-flight data, wraparound,
  pause/resume, submission failures, explicit reset, interruption/reconnection,
  and a concurrent 50,000-byte producer/consumer stream with exact byte comparison.

Simulation does not validate PIO timing, electrical connectivity or printer behavior.

## References

- [USB Printer Class 1.1](https://www.usb.org/sites/default/files/usbprint11a021811.pdf)
- [Epson LX-300+II product information](https://files.support.epson.com/pdf/lx3pii/lx3piipg.pdf)
