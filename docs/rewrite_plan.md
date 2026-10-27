# Rewrite in C11 on the pico-sdk

The firmware moves from the Arduino framework (arduino-pico 5.5.1, PlatformIO, Adafruit libraries) to plain C11 on the pico-sdk, built with CMake, with FreeRTOS SMP, TinyUSB and Pico-PIO-USB used directly.

## Decisions

- **Language:** C11 everywhere, `lib/KarabasCommon` included. The TypeScript ports in `tools/hidmapper` are not affected.
- **Build:** CMake on the pico-sdk 2.x, a uf2 per board (`-DHW_ID=1/2/3`). The host tests are built by the same CMake (ctest + Unity) instead of `pio test -e native`.
- **OS:** FreeRTOS SMP stays, with the same tasks (`APP`, `INPUT`, `USB`, `USBD`, `MATRIX`, `OLED`). The RP2040 port is built with `configSUPPORT_PICO_SYNC_INTEROP` / `configSUPPORT_PICO_TIME_INTEROP`, and TinyUSB with `CFG_TUSB_OS=OPT_OS_FREERTOS`.
- **USB VID / PID and descriptor strings** stay as they are now, so hosts do not see a new device.
- **Branch:** all the work lives in the `pico-sdk` branch, made from master (f960011). Every PR of this work targets `pico-sdk`, not master. The branch goes into master once everything is done and checked on hardware. Fixes made on master meanwhile are merged into `pico-sdk` from time to time.

## Why

The rewrite removes the workarounds that exist only because of the Arduino layer:

| Now | Cause |
|---|---|
| patches 1-6 to Adafruit TinyUSB and SdFat, `apply_patches.py` | Adafruit wrappers |
| `__usb_mutex`, the CDC patch, the patched irq that wakes `USBD` | the pico-sdk of arduino-pico has no FreeRTOS interop: a pico mutex waits with `wfe` and belongs to a core |
| the timer task at the top priority (lost wakeups) | same |
| `usb_pio_reserve` (the `ccount` cycle counter on pio1) | arduino-pico's FreeRTOS support |
| `Serial` `operator bool` yields, CDC sent only in full packets (#52), `CFG_TUSB_DEBUG` starts `Serial1` on the SD pins | the Arduino CDC / Adafruit log |
| the platform pinned to a commit, three layers of foreign versions | PlatformIO + arduino-pico + Adafruit |

## Starting point

- `src/`: ~14.7k lines (without `bitmaps.h`), `lib/KarabasCommon`: ~2.5k, own libraries (`FT812`, `OSD`, `RTC`, `IniFile`, `PioSPI`, `EspSerial`, `SegaController`, `MultiMatrixDisplay`, `GyverMenuExt`, `ElapsedTimer` (now `MsTime`)): ~4k. `lib/Pico-PIO-USB` is upstream 0.7.3 with our diagnostics (#59) and stays.
- minig: 620 KB text, 138 KB bss.
- `KarabasCommon` is C already in all but the file names (only `usb_joy_slots.h` has a class). The C++ in `src/` is `String` (~60 uses: paths, trim, extensions), ~24 lambdas (mostly `link_on` handlers), a little `std::` / `<algorithm>`, the library wrapper classes and `PrinterTrace`.

What replaces each dependency:

| Dependency | Replacement |
|---|---|
| Arduino core (`pinMode`, `digitalWrite`, `millis`, `delay`, `SPI`, `Wire`, `Serial`, `rp2040.*`) | `hardware_gpio`, `time_us_64`, `vTaskDelay`, `hardware_spi`, `hardware_i2c`, `watchdog_reboot`, `reset_usb_boot` |
| Adafruit TinyUSB (`Adafruit_USBD_MSC`, CDC `Serial`, host glue) | TinyUSB directly: own `tusb_config.h`, `usb_descriptors.c`, `tud_msc_*` / `tud_cdc_*` callbacks |
| SdFat (Adafruit fork), `PioSPI` driver | FatFs R0.15 (`FF_FS_EXFAT=1`, LFN) + own `sd_spi.c` (PIO SPI for SD1, SPI0 under the bus lock for SD2) |
| `IniFile` | `kg_ini_read` in `KarabasCommon` (tested), next to `ini_update` |
| RTClib (DS3231), SparkFun EEPROM, SparkFun PCA9536 | own small I2C drivers over `i2c_bus` |
| Adafruit GFX / LEDBackpack (HT16K33), Adafruit SSD1306 | own HT16K33 and SSD1306 (128x32, bitmaps from a framebuffer) drivers |
| GyverMenu, GyverMenuExt, GyverFIFO, StringN | the list of the other OSD screens, own ring buffer |
| `ElapsedTimer` | `uint32_t` ms and a helper |
| `String` | `snprintf` and path helpers in `file_name` |
| `tusb_xinput` | kept (C already), maybe updated to the TinyUSB version in use |

## Phase 0: preparation

1. **Baseline on Mini G**, to compare against later: boot time, core load time, MSC speed SD1 / SD2 (454 / 249 KB/s), file loader speed, the `DIAG` line, firmware size.
2. **Hardware checklist** (`docs/hw_checklist.md`, the baseline table is at its end): cores, OSD, file mounter, `.sav`, msc / msc2 and eject, gamepads / XInput / printer, PS/2, Sega, RTC / NTP, Wi-Fi, FT pages, matrix, OLED, `AT+KARABAS=`.
3. **Spike** (a separate directory, Mini G): a minimal pico-sdk project with FreeRTOS SMP and the interop, TinyUSB device CDC + MSC on a RAM disk, and our Pico-PIO-USB host with a keyboard behind the hub. This is the biggest risk and the biggest gain: if the PIO-USB host is stable here, without the `ccount` hack and the patches, with an even SOF, the rest is predictable.
   - **Result (2026-10-27, Mini G, `spike/`):** it works. SOF 1000/s with a max gap of 1005..1014 us, no disconnects, no `own`. A CDC flood from `APP` (5.4 MB in 20 s, no lost line, no `__usb_mutex`) with keys typed all the time: core0 does not stop, no key is lost. The MSC RAM disk mounts and keeps a file written by the PC. 7 hardware spinlocks free when the USB host starts, the PIO-USB programs fit without the `ccount` hack.
   - **Upstream Pico-PIO-USB 0.7.3** (same day, `spike/README.md`): works as well, with the whole usb host on pio1 (tx + rx + eop share one block, 32 words; upstream's main path since its hw rx arm at our EOP). SOF gap 1003..1015 us, the frame handler 52..100 us (216 us with a low-speed gamepad behind the hub), flood + keys clean, MSC fine, `own` 0 with the low-speed gamepad (the SE0 bug of 0.5.3 is fixed upstream). It already has our rx buffer guard, `endpoint_close`, the SE0 fix and 3 retries of a transaction (instead of our bulk OUT retry for the printer); only the diagnostics stay ours. Taken into the firmware on master in #59 (the usb host on pio1, pio0 for the firmware), the spike uses that copy now. The printer (bulk OUT) is not tested with it yet.

## Phase 1: clean up inside the current build

arduino-pico exposes the pico-sdk `hardware_*` libraries, so most of the port can be done before the platform changes, one PR at a time, with a working firmware after each:

1. **`KarabasCommon` to C:** `.cpp` -> `.c`, `usb_joy_slots` as a struct with functions, `extern "C"` guards in the headers. All tests stay green.
2. **No Arduino idioms in `src/`:** `String` -> `snprintf` + path helpers; lambdas -> static functions; `ElapsedTimer` -> ms helpers; `app_setup` without GyverMenu; own ring buffer instead of GyverFIFO.
3. **pico-sdk HAL:** gpio / time / delays (`board/pin.h`, `ms_time.h`); SPI0 through `lib/Spi0Bus` for the link and the FT812 (under the same `spi_bus_lock`; SD2's SdFat stays on Arduino's `SPI` until step 5); reboots and the free heap (`board/chip.h`); `EspSerial` and `SegaController` as C modules. `i2c_bus` on `hardware_i2c` moves to step 4: every I2C device library goes through `Wire`. `Serial` / `Print` (the CDC log) stay until phase 2. A pulse that the slow Arduino calls made long enough by themselves needs an explicit width with `gpio_put`: PROG_B became ~16 ns and the FPGA came up half reset (fixed in #67).
4. **Own I2C drivers:** DS3231, 24xx EEPROM, PCA9536, HT16K33, SSD1306 in `lib/I2cDevices` on `hardware_i2c` (`i2c_dev` replaces `Wire`); the dates, the DS3231 registers and the 5x7 font of the matrix scroll in `KarabasCommon`, tested. RTClib, SparkFun and Adafruit display libraries go away. The `i2c_bus` lock stays C++ until step 6 (`hang_diag`).
5. **Storage: SdFat -> FatFs.** The riskiest step of the phase, a PR of its own with the full checklist. Done as planned: FatFs R0.15 in `lib/FatFs` (`FILINFO.dir_ofs` and `f_readdir_at()` added for the ids; checked on FAT32 and exFAT images on a PC against a parse of the dir entries), `lib/SdSpi` (PIO SPI without DMA for now, as PioSPI was), `kg_ini_read`. On Mini G (2026-10-27): the NES loader found its last game by the old id; nanomig boots Workbench from the 14 GB HDF (after the `f_lseek` fix: a far seek took 1.16 s); MSC SD1 read 529 / write 456 kB/s (469 / 399 before), SD2 422 kB/s (375), sd2test x8 989 KB/s at 12 MHz (768).
   - `sd_spi.c`: SD SPI mode, CMD17 / 18 / 24 / 25, multi-block; PIO SPI backend (from `PioSPI`, with DMA) for SD1, SPI0 under the bus lock at 12 MHz for SD2.
   - MSC reads and writes sectors through `sd_spi`, past FatFs.
   - `IniFile` -> `kg_ini_read`.
   - **File ids:** `last_file_id` is written into the core file header (`FILE_POS_FILELOADER_FILE`) and is SdFat's `dirIndex` (+1 on exFAT). FatFs has no index, but `DIR.dptr` has the entry offset: id = `dptr / 32` (+1 on exFAT), and `sd_open_id` reads the dir with `f_readdir` up to that offset. Check on FAT32 and exFAT that the numbers match the old ones (otherwise the loader forgets the last file once).
   - Check the writes back into core files, `.sav`, `karabas.new` -> rename, and the eject (`syncDevice` -> `disk_ioctl(CTRL_SYNC)`).
6. **All `.cpp` to `.c`:** `FT812`, `OSD`, `RTC`, the apps, `hid_app`, `printer_*`. The singletons (`zxosd`, `ft`, `zxrtc`) become modules with `osd_*` / `ft_*` / `rtc_*` functions. Done: `lib/OSD`, `lib/RTC`, `lib/FT812` (only the functions in use) are C modules; the debug output is a C API (`debug_printf`); C++ references and `std::sort` are gone (`qsort`); `SpiBusGuard` / `I2cGuard` became explicit lock / unlock calls; 52 modules of `src/` are `.c`. Left C++ for phase 2: `main.cpp`, `serial_bridge.cpp` and `msc.cpp` (`Serial`, the Adafruit USB classes).

After phase 1 Arduino is left only in `main.cpp` (`setup()`, task creation) and in the Adafruit TinyUSB wrappers.

## Phase 2: the platform switch

One large PR, checked on hardware for 2-3 weeks.

Done in branch `pico-cmake` (2026-10-27): CMake with the pico-sdk 2.2.0 (TinyUSB 0.18) and FreeRTOS-Kernel V11.2.0, pinned and fetched when no path is given; the earlephilhower toolchain (its newlib has the dynamic reent the per-task `struct _reent` needs). TinyUSB's own `OPT_OS_FREERTOS` replaced `__usb_mutex`. The timer task priority assert stays (`main.c`): pico-sdk locks released from an irq still wake through it. TinyUSB 0.18 has no `tuh_edpt_close()`: `printer_host` closes in PIO-USB. On the Mini G: everything from the checklist works, core0 runs ~10 % more loops than on Arduino, the flash cache hits 97-98 %. A text OSD redraw paid ~60 us a frame in locks (also on Arduino): `spi_bus_lock` now nests with a counter and `osd_update()` locks once, several times faster.

- **CMake project:** pico-sdk, FreeRTOS-Kernel (SMP, RP2040 port, interop on), TinyUSB of the pico-sdk, `lib/Pico-PIO-USB`, `tusb_xinput`, FatFs; targets `karabas_go`, `karabas_mini`, `karabas_minig`.
- **Own `main()`:** `set_sys_clock_khz(120000)`, no stdio, the tasks created as now, then the scheduler.
- **Own `tusb_config.h`:** host limits (3 hubs, 8 devices, 16 HID, 1024-byte enumeration buffer), 4 KB MSC endpoint buffer, `CFG_TUSB_OS=OPT_OS_FREERTOS`. `CFG_TUSB_DEBUG` can go to `debug_out` instead of a UART.
- **`usb_descriptors.c`:** composite CDC + MSC, the "SD Card" / "SD Card 2" names from watchdog scratch 0.
- **`USBD`:** a task with `tud_task()` blocking on the OSAL queue. The irq patch and `__usb_mutex` go away.
- **`debug_out`** straight on `tud_cdc_write` / `tud_cdc_write_flush` (also fixes the full-packets issue of #52).
- **`hang_diag`:** `--wrap=panic` stays, the watchdog scratch layout does not change.
- **Removed:** `platformio.ini`, `apply_patches.py`, `patches/`, the `ccount` reservation, the timer task priority assert (check first that no wakeups are lost).
- **CI:** GitHub Actions with arm-none-eabi-gcc and the pico-sdk (cached), all three boards, ctest. The release job and `build*.sh` take the uf2 from the CMake build dir.

## Phase 3: finishing

- Rewrite CLAUDE.md: many sections (`__usb_mutex`, the PIO budget with `ccount`, `Serial`, lost wakeups) become history.
- Revisit the task model: what can block now instead of polling (`link_poll`, `esp_serial`).
- Compare with the phase 0 baseline. Run Go and Mini too: they are not checked with FreeRTOS yet.
- Merge `pico-sdk` into master.

## Risks

- **PIO-USB under plain FreeRTOS SMP:** SOF timing and irqs on core 1. Hence the spike first.
- **FatFs instead of SdFat:** file ids, multi-block speed, exFAT. Hence a PR of its own and the checklist.
- **Go and Mini** are rarely tested. Phase 2 should run on at least one of them.
- **Size:** phase 1 is ~10-12 PRs, phase 2 one large PR. Master keeps getting fixes meanwhile: merge them into the branch often, and write new code on master in the C style already.
