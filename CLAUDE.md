# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

This is the RP2040 (Raspberry Pi Pico) MCU firmware for the Karabas Go family of FPGA retro-computer boards. It is an Arduino-framework PlatformIO project using the earlephilhower core. The MCU acts as the FPGA's companion. It loads bitstreams from the SD card, runs the OSD menus, handles USB/PS2/Sega input and the RTC, exposes the SD card as USB mass storage, and talks to the running FPGA core over SPI.

## Build / upload

Each board has its own PlatformIO env, which sets `HW_ID`:

| Env | HW_ID | Board | Core file ext | Release script |
|---|---|---|---|---|
| `go` | 1 | Karabas Go | `.kg1` | `build.sh` |
| `mini` | 2 | Karabas Mini | `.kg2` | `build_mini.sh` |
| `minig` | 3 | Karabas Mini G | `.kg3` | `build_minig.sh` |

The firmware runs on FreeRTOS only (`main.cpp` stops the build with `#error` otherwise). It was verified on Mini G hardware; Go and Mini have not been checked on hardware with FreeRTOS. The last commit with the plain (non-FreeRTOS) build is `72d6f36` on master, if a comparison is ever needed.

- To build one board: `pio run -e go`. `pio run` without `-e` builds `minig` (`default_envs`).
- To build all boards: `pio run -e go -e mini -e minig`. A change must keep all three compiling. CI (`.github/workflows/build.yml`) builds all three and runs the tests.
- The FreeRTOS flags are in the `[rp2040]` section (`-D PIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS -D DIAG_STATS=1`, timer task settings). `karabas_setup`/`karabas_loop` run in the `APP` task pinned to core 0, and the USB host runs in the `USB` task pinned to core 1 at high priority. The Arduino `setup()` only creates the tasks; `setup1`/`loop1` are not used.
- **Tasks and shared resources:**
  - `APP` (core 0) runs the firmware loop.
  - `INPUT` (core 0, priority above `APP`) forwards usb, ps2 and joystick events to the FPGA as soon as they arrive. It hands anything that needs the UI to `APP` and waits for `APP` on a hotkey.
  - `USB` (core 1) runs the USB host.
  - SPI0 (the FPGA link and FT812) is shared. Every transfer holds the recursive `spi_bus_lock`. Hold a `SpiBusGuard` around multi-frame sequences that must not interleave, such as core load, ROM and file transfers.
  - Incoming FPGA commands are always handled in `APP`. Frames received by another task are queued and handled by `link_dispatch_pending()`.
  - `MATRIX` (core 0, above `APP`, exact 20 ms period) and `OLED` (core 0, `APP` priority, time-sliced with it) run the display frames. Each task is created only if its device was found. Matrix animation timing is counted in frames, not timers, so it stays even. While a core loads, the matrix is in `MATRIX_MODE_LOADING` and only the loader draws its progress bar. A full OLED frame is about 12 ms of busy-polled I2C. The bus runs at 400 kHz, set in `karabas_setup`. The SSD1306 is constructed so that it does not drop the clock back to 100 kHz after a frame.
  - I2C is shared by `MATRIX`, `OLED` and `APP` (RTC/EEPROM through hooks in `lib/RTC`, matrix commands from the FPGA, the Go extender). Hold an `I2cGuard` around every I2C access. **Lock order:** taking the I2C lock while holding the SPI lock is fine, but never call `spi_send()` while holding the I2C lock. FPGA command handlers run after the SPI lock is released.
  - SD and the OSD state are used only from `APP`. Keep it that way, or add a lock first.
  - **Debug output.** Only `APP` writes to the USB CDC `Serial`. Adafruit's CDC write has no lock, and concurrent writers make the host reset the port. Use the `d_print*` macros (`src/diag/debug_out`), which buffer output from other tasks, core 1 and IRQs, and all output until a terminal opens the port (8 KB), so the boot log survives a reboot. Check the port with `tud_cdc_connected()`, not `Serial`: its `operator bool` yields when nothing is connected. Do not call `Serial.print` directly outside `APP`.
  - **Lost wakeups.** pico-sdk locks (mutex, queue, semaphore) released from an IRQ wake their waiters through the FreeRTOS timer task (`xEventGroupSetBitsFromISR`). `APP` and `USB` never block, so that task runs at the highest priority (`[rp2040]` in `platformio.ini`, asserted in `main.cpp`). Do not lower it. Prefer FreeRTOS primitives or plain spinlocks between tasks.
- `DIAG_STATS=1` prints a `DIAG` line every 10 s when `debug=1`. It shows the loop rate and worst loop gap per core, the free heap and the free stack of the tasks.
- To run the unit tests: `pio test -e native`. To run a single suite: `pio test -e native -f test_kg_format`. The tests cover only `lib/KarabasCommon`.
- To upload: `pio run -e <env> -t upload` (port `/dev/ttyUSBPico`). Alternatively, copy `.pio/build/<env>/firmware.uf2` to the drive that appears in BOOTSEL mode.
- `BUILD_VER` comes from `PLATFORMIO_BUILD_FLAGS="-DBUILD_VER=..."`. The `build*.sh` scripts set it to a date stamp, then copy `firmware.uf2` and `src/karabas.ini` into sibling repos (`../karabas-go/...`, `../../karabas-mini/...`). They are release scripts, not dev loops.
- Versions are pinned on purpose. The platform is pinned to a git commit, which fixes arduino-pico at 5.5.1; the patches and the PIO / FreeRTOS workarounds are written against that version. Library versions in `lib_deps` are pinned too (GyverMenu 1.1.0 breaks `app_setup.cpp`). Only bump them deliberately and re-check.
- The pre-build script `apply_patches.py` patches Adafruit TinyUSB (`Adafruit_USBH_Host.cpp`) and the SdFat Adafruit fork (`SdFatConfig.h`) using `patches/`. It runs once per env and leaves a `.patching-done` marker beside each patched file. If a patch does not apply, the build fails. When bumping one of these libraries, regenerate its patch.
- To debug, set `debug=1` in `karabas.ini`, which enables the `d_print*` macros, or uncomment `-D WAIT_SERIAL=1` in `platformio.ini`.

## Architecture

**Dual-core split**:
- Core 0: the `APP` task runs everything else. `karabas_loop()` in `src/main.cpp` calls the module ticks in order: MSC, RTC, UI timers, buttons, UI events from `INPUT`, key repeat, OSD redraw, FPGA command dispatch, serial bridge, ESP, link poll. The order matters, so keep it when adding a tick. `INPUT`, `MATRIX` and `OLED` also run on core 0 (see the task list above).
- Core 1: the `USB` task only runs the TinyUSB host stack on PIO-USB (`tuh_task`). HID callbacks in `hid_app.cpp` push events with `spi_queue()`, which wakes `INPUT`. The keyboard report crosses cores only through `hid_kbd_report_sync()`.

**Modules** (`src/`):
- `fpga/`: `link` (SPI protocol), `bitstream` (FPGA configuration), `core_loader` (core file header, OSD items, ROMs, `do_configure`).
- `input/`: buttons, joysticks, the core 1 event queue and global hotkeys. `usb_joy` holds the two USB joystick ports, shared by HID (`drivers/*.ini`) and XInput pads: a pad keeps the port it took when plugged in.
- `ui/`: OSD state and the app table, plus the shared file list arrays.
- `storage/`: SD card, `karabas.ini` settings and USB MSC.
  - The SD card can be FAT16, FAT32 or exFAT (`SdFs` / `FsFile`, `SDFAT_FILE_TYPE=3`). exFAT has no 8.3 names, so the file lists and the saved image names use the long names (`getName`, never `getSFN`). Files in a list are opened by id: use `sd_file_id()` / `sd_open_id()`, not `dirIndex()` / `open(dir, index)`. On exFAT the id is the dir index + 1, because the first file of a subdir has index 0 and 0 means no file.
  - `FL` OSD items (ioctl file loader): `core_ioctl_send()` in `fpga/core_loader` sends the file when it is chosen, and at core start if the item has `autoload` and its file exists (the NanoMig Kickstart).
  - In the OSD, the browser of an `F` slot without an image opens in the dir of the image chosen last for a slot with the same extensions, on the file after it (disk 2 after disk 1). Esc goes back to the slot's own dir.
  - `img_mount` is the file mounter (`F` OSD items). The image of a slot stays open while the core runs. The link handlers only take the sector requests of the core (`CMD_IMG_LBA` / `CMD_IMG_SEC`), and `img_tick()` serves them from the main loop. A read sends the sector as a burst or classic frames; a write pulls the 512 bytes of the core with NOPs. Sequences are in `docs/protocol.md` section 6.
  - Physical floppy drives (Karabas Go, `board_has_floppy()`): the browser of an `F` item with `CORE_OSD_FLAG_PHYS` (the flags byte after the item type, `"physical": 1` in `core.json`) has `[ Physical DF0 ]` / `[ Physical DF1 ]` after `[ Eject ]`. The slot then keeps the file name `:phys0` / `:phys1` (`kg_phys_drive()`), and `img_mount` sends `CMD_IMG_PHYS` after the size 0 frames instead of opening a file. A drive is in one slot only.
- `periph/`: the LED matrix, OLED and USB serial bridge.
  - The bridge forwards the host's bytes to the core UART (`CMD_UART`), except `AT+KARABAS=<cmd>` lines, which the firmware answers itself: `list` (the cores on the SD card with their builds), `boot` (reboot into BOOTSEL), `msc` (reboot into USB mass storage, requested through watchdog scratch 0). Bytes that may start such a line are held back until they can't, or for 500 ms.
- `board/`: per-board code (see below).
- `main.h` is an umbrella header that includes every module header. Each module owns its state, and state is `static` unless other modules need it.

**MCU↔FPGA SPI protocol**: (full description: `docs/protocol.md` / `docs/protocol.ru.md`; keep them in sync with `config.h` when commands change)
- Every transfer is a 3-byte frame `cmd, addr, data` (`spi_send`). The FPGA replies with a 3-byte frame in the same transaction. Any non-zero reply cmd is dispatched to the handler registered with `link_on(cmd, fn)`, so the FPGA can only send data when the MCU sends something. Handlers are registered in `setup()` and `matrix_register_handlers()`. Incoming commands are ignored while the FPGA is being configured (`link_set_rx_enabled`).
- All `CMD_*` opcodes are defined in `src/config.h`, along with short comments on the multi-byte sequences for the file mounter (`CMD_IMG_*`, full description in `docs/protocol.md`) and the file loader (`CMD_IOCTL_*`).
- A new command needs matching support in the FPGA cores, which live in a separate repo.
- The SPI clock defaults to 16 MHz. A core can override it with its `spi_freq` header field, which `read_core()` passes to `link_set_freq()`.
- **Burst transfers** (`docs/burst_protocol.md`). A second chip select (`PIN_BURST_CS` = `PIN_MCU_SPI_IO0`), optional `PIN_BURST_READY` = `PIN_MCU_SPI_IO1`, a 5-byte header (type + 32-bit address) and up to 512 data bytes.
  - They are used only if the core answered the `CMD_CAPS` query that `do_configure()` sends. Otherwise everything stays on classic frames, so old cores are unaffected.
  - ROMs, the file loader and ioctl data try `link_burst_write()` first and send the rest with classic frames.
  - **MCU_IO4 (GPIO18) must stay an input:** on modded Mini boards it is the FT812 clock from the FPGA.
  - `PIN_BURST_CS` is pulled up while the MCU does not drive it, and the cores pull it up too. A floating chip select lets a core's burst receiver take classic traffic for a burst.
  - `burst=0` in `karabas.ini` skips the `CMD_CAPS` query, so everything stays on classic frames (A/B testing a core). With `debug=1` the log shows the ROM, file loader and ioctl transfer times with the link mode. The file loader also logs the CRC-32 of the sent file, which can be compared with the file on a PC.

**Core files (`.kg1/.kg2/.kg3`)**:
- Each file has a fixed binary header followed by the FPGA bitstream. Field offsets are the `FILE_POS_*` constants in `config.h`. The bitstream starts at 1024. EEPROM and switches data live at 256 and 512.
- After the bitstream comes a ROM section made of blocks. Each block has a big-endian `len` (bit 31 means external), then `addr`, then `len` bytes of payload: ROM data, or the filename of an external ROM. The writer is `make_core` in the sibling `karabas-go-tools` repo.
- Some fields are written back into the core file itself, such as the file loader dir and filename and the saved OSD switches.
- `read_core()` parses the header into the global `core` (`core_item_t` in `types.h`), including the OSD item definitions.
- `fpga_send()` (`fpga/bitstream.cpp`) streams the bitstream through the `PIN_CONF_*` pins with the `fpga_serial.pio` program fed by DMA. CCLK is `FPGA_CCLK_HZ`, 20 MHz by default. If no PIO state machine or DMA channel is free, it falls back to bit-banging.
- PIO-USB on core 1 hard-claims DMA channel 0 and PIO memory at init. Firmware code must take DMA channels from the top and claim PIO resources only after `usb_host_ready` is set.
- **PIO budget.** The PIO-USB receiver needs all 32 words of pio1 instruction memory. With FreeRTOS, arduino-pico allocates its cycle counter (`ccount`) on a PIO in `main()`, and unless something stops it, it lands on pio1 and breaks USB silently. The `usb_pio_reserve` static constructor in `main.cpp` prevents that and must stay. Nothing else may load programs into pio1. `pio_usb_host_programs_loaded()` logs an error when the PIO-USB programs do not fit.
- `fpga_serial.pio.h` is generated by pioasm and committed, so edit the `.pio` file and regenerate it (the command is in the `.pio` file).
- Core types: BOOT (the core browser), OSD (a normal core with an overlay menu), FILELOADER, and HIDDEN.

**OSD apps**:
- `osd_state` (`osd_state_e` in `types.h`) selects which `app_*.cpp` module owns the overlay and the keyboard: `app_core` (the per-core menu and file mounter), `app_core_browser`, `app_file_loader`, `app_setup`, `app_about` and `app_pad_mapper` (Setup > Map USB gamepad: learns a pad from its raw reports through `hid_capture_*` in `hid_app.cpp` and saves a driver ini with `hid_driver_save()`).
- `osd_handle()` redraws the overlay whenever the state changes, and `ui_on_keyboard()` routes keys. Both go through the `apps[]` table in `ui/osd_ui.cpp`, which is indexed by `osd_state_e`. A new screen needs an enum value and a table entry in the same order. An entry can have an optional `tick`, called from `ui_tick()` while its app owns the OSD.
- The core browser has two renderers: a text one through the FPGA OSD (`lib/OSD`) and a graphical one through the FT812 (`lib/FT812`), used when it's present and enabled.

**Per-board code**: pin maps are in `config.h`. Board behaviour lives behind the `src/board/board.h` API, implemented by `board_go.cpp` (I2C extender, Sega joysticks) and `board_mini.cpp` (Mini and Mini G, including the Mini G ESP, MIDI and FT reset lines). Outside `config.h`, `bitmaps.h` (logo selection) and `src/board/`, code must not use `#if HW_ID`. Add a `board_*` function instead.

**`lib/KarabasCommon`**: hardware-independent logic with no Arduino includes, unit-tested on the native platform. It contains `kg_format` (core file and ROM block parsing), `hid_joy_map` (`drivers/*.ini` mapping), `hid_auto_map` (the default map of a HID pad built from its report descriptor, used when there is no driver ini; see `docs/usb_gamepads.md`), `hid_learn` (learning a driver ini from raw reports, for the OSD mapper), `crc32`, `at_cmd` (the `AT+KARABAS=` line parser of the serial bridge) and `file_name` (extension match and short names of the OSD file lists). New pure logic that is worth testing goes here.

**Runtime config**: on the SD card, `karabas.ini` is read by `load_setup()` (see `src/karabas.ini` for the keys). Every line, comments included, must fit into the 80-byte read buffer (79 chars). A longer line makes `ini.validate()` reject the whole file, and the firmware silently falls back to the defaults (it logs `karabas.ini is invalid`). Keep `src/karabas.ini` free of long comments: it is shipped to users as is. USB joystick mappings are `*.ini` files in `drivers/`, described in `drivers/example_ini.txt`.

**Optional peripherals** are detected at boot and tracked by flags (`has_ft`, `has_matrix`, `has_oled`, `has_extender`, ...). Code should check the relevant flag before touching the device. The dot-matrix add-on and its ZX-side register interface are documented in `MATRIX.md`.
