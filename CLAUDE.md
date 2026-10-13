# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

This is the RP2040 (Raspberry Pi Pico) MCU firmware for the Karabas Go family of FPGA retro-computer boards. It is an Arduino-framework PlatformIO project using the earlephilhower core. The MCU acts as the FPGA's companion. It loads bitstreams from the SD card, runs the OSD menus, handles USB/PS2/Sega input and the RTC, exposes the SD card as USB mass storage, and talks to the running FPGA core over SPI.

## Build / upload

PlatformIO has a single env, `pico`. The target board is chosen at compile time with `HW_ID`:

| HW_ID | Board | Core file ext | Build script |
|---|---|---|---|
| 1 | Karabas Go | `.kg1` | `build.sh` |
| 2 | Karabas Mini | `.kg2` | `build_mini.sh` |
| 3 | Karabas Mini G | `.kg3` | `build_minig.sh` |

- `src/config.h` defaults `HW_ID` to **3** when the flag is not set. A plain `pio run` therefore builds the Mini G firmware, not the Go firmware.
- To build for a specific board: `PLATFORMIO_BUILD_FLAGS="-DHW_ID=1 -DBUILD_VER=dev" pio run`
- To upload: `pio run -t upload` (port `/dev/ttyUSBPico`). Alternatively, copy `.pio/build/pico/firmware.uf2` to the drive that appears in BOOTSEL mode.
- The `build*.sh` scripts clean, build with a date-stamped `BUILD_VER`, then copy `firmware.uf2` and `src/karabas.ini` into sibling repos (`../karabas-go/...`, `../../karabas-mini/...`). They are release scripts, not dev loops.
- `lib_extra_dirs = ../lib/` also pulls in libraries from the parent directory, in addition to the local `lib/`.
- The pre-build script `apply_patches.py` patches Adafruit TinyUSB (`Adafruit_USBH_Host.cpp`) and the SdFat Adafruit fork (`SdFatConfig.h`) using `patches/`. It runs once and leaves a `.patching-done` marker beside each patched file. If those packages are reinstalled, the marker disappears and the patch is re-applied.
- There are no unit tests (`test/` is the PlatformIO placeholder). To debug, set `debug=1` in `karabas.ini`, which enables the `d_print*` macros, or uncomment `-D WAIT_SERIAL=1` in `platformio.ini`.

## Architecture

**Dual-core split** (`src/main.cpp`):
- Core 0 (`setup`/`loop`) runs everything else: SD, FPGA config, OSD, RTC, joysticks, LED matrix and OLED, and MSC mode.
- Core 1 (`setup1`/`loop1`) only runs the TinyUSB host stack on PIO-USB (`tuh_task`). HID reports reach core 0 through `hid_app.cpp`/`hid_driver.cpp`.

**MCU↔FPGA SPI protocol**:
- Every transfer is a 3-byte frame `cmd, addr, data` (`spi_send`). The FPGA replies with a 3-byte frame in the same transaction. Any non-zero reply cmd is dispatched by `process_in_cmd()`, so the FPGA can only send data when the MCU sends something.
- All `CMD_*` opcodes are defined in `src/config.h`, along with comments on the multi-byte sequences for the file mounter (`CMD_IMG_*`) and the file loader (`CMD_IOCTL_*`).
- A new command needs matching support in the FPGA cores, which live in a separate repo.
- The SPI clock defaults to 16 MHz. A core can override it with its `spi_freq` header field.

**Core files (`.kg1/.kg2/.kg3`)**:
- Each file has a fixed binary header followed by the FPGA bitstream. Field offsets are the `FILE_POS_*` constants in `config.h`. The bitstream starts at 1024. EEPROM and switches data live at 256 and 512.
- Some fields are written back into the core file itself, such as the file loader dir and filename and the saved OSD switches.
- `read_core()` parses the header into the global `core` (`core_item_t` in `types.h`), including the OSD item definitions. `do_configure()`/`fpga_send()` then bit-bang the bitstream through the `PIN_CONF_*` pins.
- Core types: BOOT (the core browser), OSD (a normal core with an overlay menu), FILELOADER, and HIDDEN.

**OSD apps**:
- `osd_state` (`osd_state_e` in `types.h`) selects which `app_*.cpp` module owns the overlay and the keyboard: `app_core` (the per-core menu and file mounter), `app_core_browser`, `app_file_loader`, `app_setup` and `app_about`.
- `osd_handle()` redraws the overlay whenever the state changes.
- The core browser has two renderers: a text one through the FPGA OSD (`lib/OSD`) and a graphical one through the FT812 (`lib/FT812`), used when it's present and enabled.

**Per-board code**: pin maps and board-specific behaviour are wrapped in `#if HW_ID == ...` blocks across `config.h` and `main.cpp`. For example, Sega joysticks and the I/O extender exist only on the Go, and the ESP, MIDI and FT reset pins only on the Mini G. A change to one board must keep the other `HW_ID` builds compiling.

**Runtime config**: on the SD card, `karabas.ini` is read by `load_setup()` (see `src/karabas.ini` for the keys). USB joystick mappings are `*.ini` files in `drivers/`, described in `drivers/example_ini.txt`.

**Optional peripherals** are detected at boot and tracked by flags (`has_ft`, `has_matrix`, `has_oled`, `has_extender`, ...). Code should check the relevant flag before touching the device. The dot-matrix add-on and its ZX-side register interface are documented in `MATRIX.md`.

Shared globals and the SPI helpers are declared `extern` in `src/main.h`, which every app module includes.
