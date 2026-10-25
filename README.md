# Karabas Go Firmware

[![Build](https://github.com/andykarpov/karabas-go-firmware/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/andykarpov/karabas-go-firmware/actions/workflows/build.yml)

RP2040 firmware for Karabas Go.

**[USB gamepad mapper](https://andykarpov.github.io/karabas-go-firmware/hidmapper/)**: map a USB HID gamepad in the browser and download its
driver ini for `/drivers/` on the SD card. Most pads also work without one, see
[USB HID gamepads](docs/usb_gamepads.md).

## How to build

To build the firmware you have to install a VS Code + PlatformIO and all required dependencies.

## How to upload

To upload the firmware you have 2 standard choices:

1) Press Boot + Reset buttons, hold them, then release Reset. After a while you can put binary file firmware.uf2 to the mounted drive.
2) Via the VS Code, just press the Upload button from there. The firmware will be uploaded using a USB serial connection.

## Documentation

- [USB HID gamepads](docs/usb_gamepads.md): the default map and driver ini files.
- [USB XInput controllers / 8BitDo M30 2.4G](docs/xinput.md).
- [USB Printer Host / Epson LX-300+II](docs/usb_printer.md) (driver and 2048-byte queue; FPGA integration pending).
- [MCU ↔ FPGA protocol](docs/protocol.md) ([по-русски](docs/protocol.ru.md)) — SPI frames, all commands, load sequences.
- [Burst transfers](docs/burst_protocol.md) — the fast ROM / file transfer mode.
- [LED matrix add-on](MATRIX.md).
- Core files (`.kg1` / `.kg2` / `.kg3`) and `core.json`: [karabas-go-tools/make_core](https://github.com/andykarpov/karabas-go-tools/tree/master/make_core).
