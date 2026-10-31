# Karabas Go Firmware

[![Build](https://github.com/andykarpov/karabas-go-firmware/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/andykarpov/karabas-go-firmware/actions/workflows/build.yml)

English | [Русский](README.ru.md)

Firmware for the RP2040 (Raspberry Pi Pico) MCU of the Karabas Go family of FPGA retro computers.
The MCU is the companion of the FPGA. It loads the cores (bitstreams) from the SD card, draws the
on-screen menus, handles the keyboards, mice and joysticks and the real-time clock, and talks to
the running core over SPI.

**[USB gamepad mapper](https://andykarpov.github.io/karabas-go-firmware/hidmapper/)**: map a USB
HID gamepad in the browser and download its driver ini for `/drivers/` on the SD card. It is
rarely needed now: most pads work without an ini, and the board maps a pad itself in Setup >
Map USB gamepad (text and graphic menus), see [USB HID gamepads](docs/usb_gamepads.md).

## Boards

| Board | Core files | Firmware file in a release |
| --- | --- | --- |
| Karabas Go | `*.kg1` | `karabas-go-firmware.uf2` |
| Karabas Mini | `*.kg2` | `karabas-mini-firmware.uf2` |
| Karabas Mini G | `*.kg3` | `karabas-minig-firmware.uf2` |

## Features

- **Core loading.** The core browser lists the cores on the SD card: a graphical menu on the FT812
  video chip when the board has it, or a text menu otherwise. It can start a chosen core
  automatically after a timeout. The bitstream goes to the FPGA through PIO and DMA, then the
  ROMs of the core follow.
- **OSD.** Cores with an on-screen menu get their options, switches and hotkeys from the core file.
  A long menu scrolls; a core can split it into sub-menus (pages, nested), separators and options
  shown or enabled only with a value of another option (the `core.json` keys are in
  [make_core](https://github.com/andykarpov/karabas-go-tools/blob/master/make_core/README.md#sub-menus-pages-and-conditional-items)).
  Disk images are mounted from the SD card and stay open while the core runs, with reads and
  writes. File loaders are supported too. On Karabas Go, physical floppy drives can be selected
  instead of an image.
- **Input.**
  - USB keyboards (including NKRO) and mice.
  - USB HID gamepads: mapped by default from their HID descriptor, or with a driver ini
    learned in the OSD (or from the [browser mapper](https://andykarpov.github.io/karabas-go-firmware/hidmapper/)).
  - XInput pads (the Xbox 360 wired protocol, e.g. 8BitDo M30 2.4G).
  - PS/2 keyboards.
  - Sega joysticks on the joystick ports (Karabas Go).
  - Every joystick reaches the core as a Sega Mega Drive 6-button pad.
- **USB host.** Runs on the second RP2040 core over PIO-USB, hubs included. A USB printer
  driver (Epson LX-300+II) is in progress.
- **Real-time clock**, set in the OSD Setup menu and passed to the core.
- **Wi-Fi setup** of the ESP8266 in the OSD Setup menu (core browser): scan, password, join. The
  module keeps the network and joins it on start; its CTS flow control is turned on.
- **SD card as a USB drive.** The PC sees the SD card as a USB mass storage device, so the card
  can stay in the board.
- **USB serial bridge.** The USB serial port of the board is wired to the UART of the core, and a
  few `AT+KARABAS=` commands control the firmware itself.
- **Optional peripherals**, detected at boot: FT812, OLED display, LED matrix, I2C extender (Go),
  ESP8266, MIDI (Mini G).
- **Fast transfers.** Cores that support it receive ROMs and files in bursts of up to 512 bytes
  over a second chip select.

## Getting the firmware

Each build of `master` that changes the firmware becomes a
[release](https://github.com/andykarpov/karabas-go-firmware/releases) with the three `.uf2` files.
The same files go to the `firmware/` folders of
[karabas-go](https://github.com/andykarpov/karabas-go) and
[karabas-mini](https://github.com/andykarpov/karabas-mini).

### Test firmware from a pull request

CI builds the firmware of every PR. Anyone signed in to GitHub can download it, no rights to
the repository are needed.

1. Open the PR page and go to the **Checks** tab.
2. Pick **Build** on the left, then **Summary** at the top of the left column.
3. At the bottom of the page, under **Artifacts**, download the archive for your board:

   | Board | Artifact |
   |---|---|
   | Karabas Go | `firmware-go` |
   | Karabas Mini | `firmware-mini` |
   | Karabas Mini G | `firmware-minig` |

4. Unzip it and flash `firmware.uf2` as described in
   [Updating the firmware](#updating-the-firmware).

**Checks** shows the run of the latest commit of the PR, the newest build. It is a test build,
not a release: to go back to the stable firmware, flash the `.uf2` from
[Releases](https://github.com/andykarpov/karabas-go-firmware/releases). GitHub keeps the
artifacts for 90 days.

## Updating the firmware

1. Put the RP2040 into BOOTSEL mode: hold the **Boot** button, press and release **Reset**, then
   release **Boot**. Alternatively, send `AT+KARABAS=boot` to the USB serial port of the board.
2. A USB drive named `RPI-RP2` appears. Copy the `.uf2` file for your board to it.
3. The board restarts with the new firmware.

## SD card

| Path | What |
| --- | --- |
| `/boot.kg1` (`.kg2`, `.kg3`) | the core browser, loaded at power on |
| `/*.kg1` (`.kg2`, `.kg3`) | the cores of the board |
| `/karabas.ini` | settings, optional, see below |
| `/drivers/*.ini` | USB gamepad drivers, optional ([format](docs/usb_gamepads.md#driver-ini)) |

The card can be FAT16, FAT32 or exFAT. The cores are built with
[make_core](https://github.com/andykarpov/karabas-go-tools/tree/master/make_core) from
[karabas-go-tools](https://github.com/andykarpov/karabas-go-tools).

### karabas.ini

The file is optional: without it the firmware uses the defaults below. Every key can be changed
in the menus: Setup > Settings of the text core browser (`[setup]`, `[autoload]`, `[matrix]`,
`enabled` / `video_mode` of `[ft812]`) and the graphic setup (all of them). A save creates the
file, or writes only the changed keys and keeps the rest of it. Lines must be at most 79
characters long, or the whole file is ignored.

| Section | Key | Default | Meaning |
| --- | --- | --- | --- |
| `[setup]` | `debug` | `0` | `1`: debug log on the USB serial port |
| | `dvi_only` | `0` | `1`: the cores drop the HDMI frames (audio, info) and output plain DVI |
| | `tz` | `+00:00` | UTC offset for NTP sync, set in Setup RTC / the Clock page, e.g. `+03:00`, `-04:30`; no daylight saving |
| `[ft812]` | `enabled` | `1` | `1`: graphical core browser on the FT812 |
| | `video_mode` | `1` | FT812 video mode: `0` 640x480@60, `1` 800x600@60 |
| | `sound`, `click` | `1`, `1` | menu sounds |
| | `time`, `date`, `char` | `1`, `1`, `1` | show the clock, the date and the Karabas picture in the graphical browser |
| | `color_bg`, `color_gradient`, `color_button`, `color_active`, `color_text`, `color_text_active`, `color_copyright` | `010101`, `0000c8`, `202026`, `0000c8`, `ffffff`, `ffffff`, `787878` | colors (hex RGB) |
| | `3d_buttons` | `0` | `1`: buttons with a shadow and a light edge (not in 800x600: too heavy for the FT812 there) |
| | `mouse_speed` | `3` | mouse pointer speed in the graphic menu, 1..5 |
| `[matrix]` | `enabled`, `brightness`, `clock_*`, `meter_*` | `1`, `0`, ... | the LED matrix, see [docs/matrix.md](docs/matrix.md) |
| `[autoload]` | `enabled`, `timeout`, `core` | `0`, `15`, none | start `core` after `timeout` seconds |

Example:

```ini
[setup]
debug=0
tz=+03:00

[ft812]
video_mode=0

[autoload]
enabled=1
timeout=5
core=tsconf
```

## Controls

| Keys | Joystick | Action |
| --- | --- | --- |
| Menu (Win) + Esc | Start + C, Start + Select | open / close the OSD of the running core |
| Ctrl + Alt + Backspace | Start + X | restart the MCU and go back to the core browser |
| arrows, Enter, Esc | D-pad, A / B, C or Select | navigate the menus (a held direction repeats); not in the gamepad mapping itself |
| Left | Left | back, where Left has nothing else to do: Setup menu, About, Wi-Fi status and network list, the tabs of the graphic setup; on the first core of the graphic browser, onto the Setup button (Enter opens it, Right goes back) |
| digits | Up / Down | the digit under the cursor of a date / time field |
| typing | Up / Down, Right, Left | a Wi-Fi text field: change the last char, add one, delete one |
| `S` | Start (core browser) | Setup: clock, Wi-Fi and Settings (core browser), USB gamepad mapper; in the graphic browser, the graphic setup (settings, video mode, color themes) |
| `A` (core OSD) | | About |
| Enter, Right (core OSD) | A / B, Right | open the sub-menu of a `►` item |
| Esc (core OSD) | C or Select | back from a sub-menu to the menu above it |
| Delete / Backspace (core OSD) | | eject the disk image of the selected drive |
| Space (graphical browser) | | switch to the text browser |
| mouse (graphical browser, Setup) | | hover, click, wheel; right button = Esc |
| `G` (text browser) | | switch to the graphical browser, also when it is off in `karabas.ini` |

Cores can define their own OSD hotkeys.

## On-board buttons

Each core sets what the buttons **BTN1** and **BTN2** do (`buttons` in its `core.json`, see
`make_core` in karabas-go-tools): pass them to the core (reset, NMI... as the core decides; the
default), open the OSD, go back to the menu, reload the core, reboot into BOOTSEL or into USB mass
storage, on a press or on a hold. In the boot menu BTN1 reboots into BOOTSEL and BTN2 into USB mass
storage (a hold of 2 s: with the second SD card).

## SD card as a USB drive

Press **BTN2** in the boot menu, or send `AT+KARABAS=msc` to the serial port. The board then shows
its SD card to the PC as a USB drive, and nothing else runs; the screen says so. Eject the drive on
the PC to restart normally.

The second SD card (the slot of the FPGA, used by the cores) works the same way: hold **BTN2** in
the boot menu for 2 seconds, or send `AT+KARABAS=msc2`. The drive is named "SD Card 2" and is
slower (about 300 KB/s). Without a card in the slot the board goes back to the menu.

## USB serial bridge

Bytes from the PC go to the UART of the running core. Lines starting with `AT+KARABAS=` are
answered by the firmware:

| Command | Action |
| --- | --- |
| `AT+KARABAS=list` | list the cores on the SD card with their builds |
| `AT+KARABAS=version` | `+KARABAS:<firmware>;<board>;<core id>;<core build>`: the firmware build, the board (1 Go, 2 Mini, 3 Mini G) and the running core |
| `AT+KARABAS=boot` | restart into BOOTSEL mode, to update the firmware |
| `AT+KARABAS=msc` | restart with the SD card as a USB drive |
| `AT+KARABAS=msc2` | restart with the second SD card (the slot of the FPGA) as a USB drive |
| `AT+KARABAS=sd2test` | core browser only: reads the second SD card at a few SPI clocks (a check of the card and the link) |
| `AT+KARABAS=esp` | core browser only: the bridge goes to the ESP8266, for its own AT commands (a CR, LF or CR LF from the terminal goes as CR LF) |
| `AT+KARABAS=uart` | back to the UART of the core |

## Building from source

The firmware is C11 on the pico-sdk with FreeRTOS SMP and TinyUSB, built with CMake and the
earlephilhower arm-none-eabi toolchain ([pico-quick-toolchain](https://github.com/earlephilhower/pico-quick-toolchain)
4.1.0: its newlib is needed). The pico-sdk and FreeRTOS versions are pinned in
`CMakeLists.txt` and fetched unless `PICO_SDK_PATH` / `FREERTOS_KERNEL_PATH` point to them.

```sh
cmake -S . -B build -G Ninja                  # add -DPICO_TOOLCHAIN_PATH=... if the toolchain is not on PATH
ninja -C build                                # all boards
ninja -C build karabas_minig                  # one board: karabas_go, karabas_mini, karabas_minig
cmake -S test -B build-test -G Ninja && ninja -C build-test && ctest --test-dir build-test   # unit tests
```

The result is `build/karabas_<board>.uf2`: copy it to the `RPI-RP2` drive of the board in
BOOTSEL mode (`AT+KARABAS=boot` gets there over USB). CI
([build.yml](.github/workflows/build.yml)) builds all boards, runs the tests and makes the
releases. The gamepad mapper lives in [`tools/hidmapper`](tools/hidmapper) and is published to
GitHub Pages by [hidmapper.yml](.github/workflows/hidmapper.yml).

[CLAUDE.md](CLAUDE.md) describes the architecture: tasks and locks, modules, the core file format
and the conventions of the code.

## Documentation

- [USB HID gamepads](docs/usb_gamepads.md): the default map, the OSD mapper and driver ini files.
- [USB XInput controllers / 8BitDo M30 2.4G](docs/xinput.md).
- [USB printer host / Epson LX-300+II](docs/usb_printer.md) (driver and 2048-byte queue; FPGA
  integration pending).
- [MCU ↔ FPGA protocol](docs/protocol.md) ([по-русски](docs/protocol.ru.md)): SPI frames, all
  commands, load sequences.
- [Burst transfers](docs/burst_protocol.md): the fast ROM / file transfer mode.
- [LED matrix add-on](docs/matrix.md).
- Core files (`.kg1` / `.kg2` / `.kg3`) and `core.json`:
  [karabas-go-tools/make_core](https://github.com/andykarpov/karabas-go-tools/tree/master/make_core).

## License

[MIT](LICENSE).
