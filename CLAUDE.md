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

- To build one board: `pio run -e go`. A plain `pio run` builds `minig` (`default_envs`).
- To build all boards: `pio run -e go -e mini -e minig`. A change must keep all three compiling. CI (`.github/workflows/build.yml`) builds all three and runs the tests.
- The FreeRTOS prototype is built by the `go_rtos`, `mini_rtos` and `minig_rtos` envs (`-D PIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS -D DIAG_STATS=1`). It is the same firmware, but `karabas_setup`/`karabas_loop` run in the `APP` task pinned to core 0, and the USB host runs in the `USB` task pinned to core 1 at high priority. Code that must differ under FreeRTOS checks `__FREERTOS`.
- **Tasks and shared resources in the FreeRTOS build:**
  - `APP` (core 0) runs the firmware loop.
  - `INPUT` (core 0, priority above `APP`) forwards usb, ps2 and joystick events to the FPGA as soon as they arrive. It hands anything that needs the UI to `APP` and waits for `APP` on a hotkey.
  - `USB` (core 1) runs the USB host.
  - SPI0 (the FPGA link and FT812) is shared. Every transfer holds the recursive `spi_bus_lock`. Hold a `SpiBusGuard` around multi-frame sequences that must not interleave, such as core load, ROM and file transfers.
  - Incoming FPGA commands are always handled in `APP`. Frames received by another task are queued and handled by `link_dispatch_pending()`.
  - I2C (RTC, matrix, OLED, the Go extender), SD and the OSD state are used only from `APP`. Keep it that way, or add a lock first.
  - In the plain build the locks are no-ops and everything runs in the main loop as before.
  - **Lost wakeups.** pico-sdk locks (mutex, queue, semaphore) released from an IRQ wake their waiters through the FreeRTOS timer task (`xEventGroupSetBitsFromISR`). `APP` and `USB` never block, so that task runs at the highest priority (`[rtos]` in `platformio.ini`, asserted in `main.cpp`). Do not lower it. Prefer FreeRTOS primitives or plain spinlocks between tasks.
- `DIAG_STATS=1` prints a `DIAG` line every 10 s when `debug=1`. It shows the loop rate and worst loop gap per core, the free heap and, under FreeRTOS, the free stack of the tasks.
- To run the unit tests: `pio test -e native`. To run a single suite: `pio test -e native -f test_kg_format`. The tests cover only `lib/KarabasCommon`.
- To upload: `pio run -e <env> -t upload` (port `/dev/ttyUSBPico`). Alternatively, copy `.pio/build/<env>/firmware.uf2` to the drive that appears in BOOTSEL mode.
- `BUILD_VER` comes from `PLATFORMIO_BUILD_FLAGS="-DBUILD_VER=..."`. The `build*.sh` scripts set it to a date stamp, then copy `firmware.uf2` and `src/karabas.ini` into sibling repos (`../karabas-go/...`, `../../karabas-mini/...`). They are release scripts, not dev loops.
- Library versions in `lib_deps` are pinned on purpose. GyverMenu 1.1.0 breaks `app_setup.cpp`.
- The pre-build script `apply_patches.py` patches Adafruit TinyUSB (`Adafruit_USBH_Host.cpp`) and the SdFat Adafruit fork (`SdFatConfig.h`) using `patches/`. It runs once per env and leaves a `.patching-done` marker beside each patched file. If a patch does not apply, the build fails. When bumping one of these libraries, regenerate its patch.
- To debug, set `debug=1` in `karabas.ini`, which enables the `d_print*` macros, or uncomment `-D WAIT_SERIAL=1` in `platformio.ini`.

## Architecture

**Dual-core split**:
- Core 0 (`setup`/`loop`) runs everything else. `src/main.cpp` only does init and calls the module ticks in order: MSC, RTC, UI timers, buttons, joysticks, OSD redraw, input event queue, serial bridge, ESP, matrix, OLED. The order matters, so keep it when adding a tick.
- Core 1 (`setup1`/`loop1`) only runs the TinyUSB host stack on PIO-USB (`tuh_task`). HID callbacks in `hid_app.cpp` push events with `spi_queue()`, and core 0 drains them in `input_drain_events()`. The keyboard report crosses cores only through `hid_kbd_report_sync()`.

**Modules** (`src/`):
- `fpga/`: `link` (SPI protocol), `bitstream` (FPGA configuration), `core_loader` (core file header, OSD items, ROMs, `do_configure`).
- `input/`: buttons, joysticks, the core 1 event queue and global hotkeys.
- `ui/`: OSD state and the app table, plus the shared file list arrays.
- `storage/`: SD card, `karabas.ini` settings and USB MSC.
- `periph/`: the LED matrix, OLED and USB serial bridge.
- `board/`: per-board code (see below).
- `main.h` is an umbrella header that includes every module header. Each module owns its state, and state is `static` unless other modules need it.

**MCU↔FPGA SPI protocol**:
- Every transfer is a 3-byte frame `cmd, addr, data` (`spi_send`). The FPGA replies with a 3-byte frame in the same transaction. Any non-zero reply cmd is dispatched to the handler registered with `link_on(cmd, fn)`, so the FPGA can only send data when the MCU sends something. Handlers are registered in `setup()` and `matrix_register_handlers()`. Incoming commands are ignored while the FPGA is being configured (`link_set_rx_enabled`).
- All `CMD_*` opcodes are defined in `src/config.h`, along with comments on the multi-byte sequences for the file mounter (`CMD_IMG_*`) and the file loader (`CMD_IOCTL_*`).
- A new command needs matching support in the FPGA cores, which live in a separate repo.
- The SPI clock defaults to 16 MHz. A core can override it with its `spi_freq` header field, which `read_core()` passes to `link_set_freq()`.

**Core files (`.kg1/.kg2/.kg3`)**:
- Each file has a fixed binary header followed by the FPGA bitstream. Field offsets are the `FILE_POS_*` constants in `config.h`. The bitstream starts at 1024. EEPROM and switches data live at 256 and 512.
- After the bitstream comes a ROM section made of blocks. Each block has a big-endian `len` (bit 31 means external), then `addr`, then `len` bytes of payload: ROM data, or the filename of an external ROM. The writer is `make_core` in the sibling `karabas-go-tools` repo.
- Some fields are written back into the core file itself, such as the file loader dir and filename and the saved OSD switches.
- `read_core()` parses the header into the global `core` (`core_item_t` in `types.h`), including the OSD item definitions.
- `fpga_send()` (`fpga/bitstream.cpp`) streams the bitstream through the `PIN_CONF_*` pins with the `fpga_serial.pio` program fed by DMA. CCLK is `FPGA_CCLK_HZ`, 20 MHz by default. If no PIO state machine or DMA channel is free, it falls back to bit-banging.
- PIO-USB on core 1 hard-claims DMA channel 0 and PIO memory at init. Firmware code must take DMA channels from the top and claim PIO resources only after `usb_host_ready` is set.
- **PIO budget.** The PIO-USB receiver needs all 32 words of pio1 instruction memory. Under FreeRTOS, arduino-pico allocates its cycle counter (`ccount`) on a PIO in `main()`, and unless something stops it, it lands on pio1 and breaks USB silently. The `usb_pio_reserve` static constructor in `main.cpp` prevents that and must stay. Nothing else may load programs into pio1. `pio_usb_host_programs_loaded()` logs an error when the PIO-USB programs do not fit.
- `fpga_serial.pio.h` is generated by pioasm and committed, so edit the `.pio` file and regenerate it (the command is in the `.pio` file).
- Core types: BOOT (the core browser), OSD (a normal core with an overlay menu), FILELOADER, and HIDDEN.

**OSD apps**:
- `osd_state` (`osd_state_e` in `types.h`) selects which `app_*.cpp` module owns the overlay and the keyboard: `app_core` (the per-core menu and file mounter), `app_core_browser`, `app_file_loader`, `app_setup` and `app_about`.
- `osd_handle()` redraws the overlay whenever the state changes, and `ui_on_keyboard()` routes keys. Both go through the `apps[]` table in `ui/osd_ui.cpp`, which is indexed by `osd_state_e`. A new screen needs an enum value and a table entry in the same order.
- The core browser has two renderers: a text one through the FPGA OSD (`lib/OSD`) and a graphical one through the FT812 (`lib/FT812`), used when it's present and enabled.

**Per-board code**: pin maps are in `config.h`. Board behaviour lives behind the `src/board/board.h` API, implemented by `board_go.cpp` (I2C extender, Sega joysticks) and `board_mini.cpp` (Mini and Mini G, including the Mini G ESP, MIDI and FT reset lines). Outside `config.h`, `bitmaps.h` (logo selection) and `src/board/`, code must not use `#if HW_ID`. Add a `board_*` function instead.

**`lib/KarabasCommon`**: hardware-independent logic with no Arduino includes, unit-tested on the native platform. It contains `kg_format` (core file and ROM block parsing) and `hid_joy_map` (`drivers/*.ini` mapping). New pure logic that is worth testing goes here.

**Runtime config**: on the SD card, `karabas.ini` is read by `load_setup()` (see `src/karabas.ini` for the keys). USB joystick mappings are `*.ini` files in `drivers/`, described in `drivers/example_ini.txt`.

**Optional peripherals** are detected at boot and tracked by flags (`has_ft`, `has_matrix`, `has_oled`, `has_extender`, ...). Code should check the relevant flag before touching the device. The dot-matrix add-on and its ZX-side register interface are documented in `MATRIX.md`.
