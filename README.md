# Karabas Go Firmware

RP2040 firmware for Karabas Go.

## How to build

To build the firmware you have to install a VS Code + PlatformIO and all required dependencies.

## How to upload

To upload the firmware you have 2 standard choices:

1) Press Boot + Reset buttons, hold them, then release Reset. After a while you can put binary file firmware.uf2 to the mounted drive.
2) Via the VS Code, just press the Upload button from there. The firmware will be uploaded using a USB serial connection.

## Documentation

- [USB XInput controllers / 8BitDo M30 2.4G](docs/xinput.md).
- [MCU ↔ FPGA protocol](docs/protocol.md) ([по-русски](docs/protocol.ru.md)) — SPI frames, all commands, load sequences.
- [Burst transfers](docs/burst_protocol.md) — the fast ROM / file transfer mode.
- [LED matrix add-on](MATRIX.md).
- Core files (`.kg1` / `.kg2` / `.kg3`) and `core.json`: [karabas-go-tools/make_core](https://github.com/andykarpov/karabas-go-tools/tree/master/make_core).
