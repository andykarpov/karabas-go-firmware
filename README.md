# Karabas Go Firmware

[![Build](https://github.com/andykarpov/karabas-go-firmware/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/andykarpov/karabas-go-firmware/actions/workflows/build.yml)

English | [Русский](README.ru.md)

Firmware for the RP2040 (Raspberry Pi Pico) MCU of the Karabas Go family of FPGA retro computers.
The MCU is the companion of the FPGA. It loads the cores (bitstreams) from the SD card, draws the
on-screen menus, handles the keyboards, mice and joysticks and the real-time clock, and talks to
the running core over SPI.

**[USB gamepad mapper](https://andykarpov.github.io/karabas-go-firmware/hidmapper/)**: map a USB
HID gamepad in the browser and download its driver ini for `/drivers/` on the SD card. Most pads
work without one, see [USB HID gamepads](docs/usb_gamepads.md).

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
  Disk images are mounted from the SD card and stay open while the core runs, with reads and
  writes. File loaders are supported too. On Karabas Go, physical floppy drives can be selected
  instead of an image.
- **Input.**
  - USB keyboards (including NKRO) and mice.
  - USB HID gamepads: mapped by default from their HID descriptor, or with a driver ini
    from the [mapper](https://andykarpov.github.io/karabas-go-firmware/hidmapper/) or the OSD.
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
  over a second chip select (`burst=0` in `karabas.ini` turns it off).

## Getting the firmware

Each build of `master` that changes the firmware becomes a
[release](https://github.com/andykarpov/karabas-go-firmware/releases) with the three `.uf2` files.
The same files go to the `firmware/` folders of
[karabas-go](https://github.com/andykarpov/karabas-go) and
[karabas-mini](https://github.com/andykarpov/karabas-mini), together with `karabas.ini`.

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
| `/karabas.ini` | settings, see below |
| `/drivers/*.ini` | USB gamepad drivers, optional ([format](drivers/example_ini.txt)) |

The card can be FAT16, FAT32 or exFAT. The cores are built with
[make_core](https://github.com/andykarpov/karabas-go-tools/tree/master/make_core) from
[karabas-go-tools](https://github.com/andykarpov/karabas-go-tools).

### karabas.ini

[`src/karabas.ini`](src/karabas.ini) is the default. Lines must be at most 79 characters long,
or the whole file is ignored. Most of `[setup]` and `[autoload]`, and `enabled` / `video_mode`
of `[ft812]`, can also be changed in Setup > Settings of the core browser; it writes only the
changed keys and keeps the rest of the file.

| Section | Key | Meaning |
| --- | --- | --- |
| `[setup]` | `debug` | `1`: debug log on the USB serial port |
| | `debug_hid` | `1`: also log raw USB HID reports and descriptors |
| | `burst` | `0`: never use burst transfers |
| | `dvi_only` | `1`: the cores drop the HDMI frames (audio, info) and output plain DVI |
| `[ft812]` | `enabled` | `1`: graphical core browser on the FT812 |
| | `video_mode` | FT812 video mode (0..14) |
| | `sound`, `click` | menu sounds |
| | `time`, `date`, `char` | show the clock, the date and the Karabas picture in the graphical browser |
| | `color_*`, `3d_buttons` | colors (hex RGB) and button style |
| `[autoload]` | `enabled`, `timeout`, `core` | start `core` after `timeout` seconds |

## Controls

| Keys | Joystick | Action |
| --- | --- | --- |
| Menu (Win) + Esc | Start + C | open / close the OSD of the running core |
| Ctrl + Alt + Backspace | Start + X | restart the MCU and go back to the core browser |
| arrows, Enter, Esc | D-pad and buttons | navigate the menus |
| `S` | | Setup: clock, Wi-Fi and Settings (core browser), USB gamepad mapper; in the graphic browser, the graphic setup (settings, video mode, color themes) |
| `A` (core OSD) | | About |
| Delete / Backspace (core OSD) | | eject the disk image of the selected drive |
| Space (graphical browser) | | switch to the text browser |

Cores can define their own OSD hotkeys.

## SD card as a USB drive

Hold the second button (**BTN2**) while powering on, or send `AT+KARABAS=msc` to the serial port.
The board then shows its SD card to the PC as a USB drive, and nothing else runs. Eject the drive
on the PC, or press the first button, to restart normally.

## USB serial bridge

Bytes from the PC go to the UART of the running core. Lines starting with `AT+KARABAS=` are
answered by the firmware:

| Command | Action |
| --- | --- |
| `AT+KARABAS=list` | list the cores on the SD card with their builds |
| `AT+KARABAS=boot` | restart into BOOTSEL mode, to update the firmware |
| `AT+KARABAS=msc` | restart with the SD card as a USB drive |
| `AT+KARABAS=esp` | core browser only: the bridge goes to the ESP8266, for its own AT commands |
| `AT+KARABAS=uart` | back to the UART of the core |

## Building from source

The firmware is a [PlatformIO](https://platformio.org/) project (Arduino framework, earlephilhower
RP2040 core, FreeRTOS). Platform and library versions are pinned.

```sh
pio run -e go                    # Karabas Go
pio run -e mini                  # Karabas Mini
pio run -e minig                 # Karabas Mini G (also the default)
pio run -e go -e mini -e minig   # all boards
pio test -e native               # unit tests of lib/KarabasCommon
pio run -e minig -t upload       # build and upload over USB
```

The result is `.pio/build/<env>/firmware.uf2`. CI
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
- [LED matrix add-on](MATRIX.md).
- Core files (`.kg1` / `.kg2` / `.kg3`) and `core.json`:
  [karabas-go-tools/make_core](https://github.com/andykarpov/karabas-go-tools/tree/master/make_core).

## License

[MIT](LICENSE).
