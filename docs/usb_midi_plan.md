# USB MIDI keyboards

A USB MIDI keyboard on the USB host of the MCU, its events passed to the FPGA core. Status: steps 1-4 (the firmware) done on the branch `usb-midi`, not checked on hardware yet; steps 5-6 (FPGA) not started.

## What there is now

- **TinyUSB 0.18 (pico-sdk 2.2.0) has no MIDI host**, only `midi_device`. The firmware gets its own class driver, as `usb/printer_host` and the XInput one, registered in `usbh_app_driver_get_cb()` (`usb/host_drivers.c`). The pinned SDK stays.
- **The boards have a Dream SAM2695 synth**, fed by the core: `MIDI_TX` (31250 baud) and `MIDI_CLK` (12 MHz). The firmware only drives `PIN_MIDI_RESET` (Mini G, `board_mini.c`). Profi sends its MIDI out through port A bit 2 of the AY. MIDI goes only from the core to the synth today.
- **USB-MIDI** is simple: an Audio class interface with the MIDI Streaming subclass (1 / 3), a bulk IN endpoint, 4-byte event packets (CIN + 3 MIDI bytes).

## Architecture

```
USB keyboard -> [USB task, core 1] midi_host: IN endpoint, 4-byte packets
   -> raw MIDI bytes -> spi_queue(CMD_MIDI, ...) -> INPUT -> FPGA
   -> core: its own MIDI IN (a UART) and / or thru to the SAM2695
```

## Steps

1. **`usb/midi_host.c`** (core 1, in RAM): `open()` takes the MIDI Streaming interface with an IN endpoint, bulk or interrupt (some vendors), no OUT for now. It costs 1 endpoint of the 32 of the PIO-USB pool. The packets become a plain MIDI byte stream by their CIN, without running status (simpler for the core). One device, cable 0, the other cables ignored. SysEx, Active Sensing (`FE`) and Clock (`F8`) are dropped for now: every byte costs a frame on the link.
2. **`CMD_MIDI`** (e.g. `0x16`): frame `16, seq, byte`, one frame per byte, the low bits of `addr` count the bytes so the core can see a loss. A note is 3 frames, ~10 us, far below the 1 ms a message takes on MIDI itself.
3. **Caps bit 4 = MIDI IN.** `CMD_MIDI` goes only to cores that answered `CMD_CAPS` with it, so old cores get no extra traffic.
4. **Docs and diagnostics:** `docs/protocol.md` / `protocol.ru.md`, `config.h`; with `debug=1` the mount / unmount and VID:PID in the log, the received / dropped counters on the `DIAG` line.
5. **FPGA module** in `karabas-go-modules` (e.g. `kg_midi_in`): takes the frames from `mcu.vhd` and gives the bytes (a) to a FIFO / register for the MIDI port of a core, (b) optionally to a 31250 baud UART tx, a thru to the SAM2695.
6. **First core: the boot core with the thru to the SAM2695.** The keyboard plays the on-board synth, which checks the whole chain without a MIDI port of an emulated machine. The real target is an Atari ST core (it does not exist yet).

Steps 1-4 are one firmware PR, steps 5-6 are in the modules / core repos. MIDI OUT to USB devices (a core playing an external USB synth) is left for later.

## Done in the firmware

- `usb/midi_host.c`: the class driver, registered third in `usbh_app_driver_get_cb()`. It takes the MS interface offered alone, or inside an IAD that starts with the Audio Control one. The IN transfer is re-armed from its callback, so the idle USB loop gains nothing; a stall stops it (PIO-USB cannot clear one).
- `lib/common/usb_midi` (`kg_usb_midi_packet()`, test `test_usb_midi`): a 4-byte packet into 0..3 MIDI bytes.
- `CMD_MIDI = 0x16`, `CAPS_MIDI = 0x10` (`fpga/link.h`); bytes go through `spi_queue()` to `INPUT`, also while the OSD is open.
- `debug=1`: `USB MIDI mounted VID:PID ...` / `disconnected`, failed endpoint opens (a full pool); the `DIAG` line gets `midi on rx N sent N skipped N` once a keyboard was seen (skipped: the core has no caps bit 4).

## Risks

- **Endpoints:** a keyboard is often a composite device (MIDI + HID + Audio Control). With hubs and pads the PIO-USB pool may have no room left for MIDI; the log shows it.
- **Descriptors:** some keyboards declare MIDI Streaming oddly (interrupt endpoints instead of bulk and so on).
- **Latency:** the IN endpoint is polled every frame on full speed, plus the `INPUT` queue: ~1-2 ms expected.
- **Test keyboard:** Akai MPK Mini 3.
