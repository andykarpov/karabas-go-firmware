# USB XInput controllers

[Русская версия](xinput.ru.md)

The firmware uses [Ryzee119/tusb_xinput](https://github.com/Ryzee119/tusb_xinput),
copied into `third_party/tusb_xinput` (with its license), as an additional TinyUSB host class driver.
The copy keeps the `bool open()` API that the class drivers of TinyUSB 0.18 (pico-sdk 2.2.0)
have; the newer upstream `uint16_t open()` API is for a later TinyUSB.
The target device is an **8BitDo M30 2.4G connected by a USB data cable**.
Bluetooth and the M30's 2.4G receiver are not implemented by this integration.
The actual controller must enumerate in XInput mode; a HID-mode device still uses
the existing HID driver mechanism.

XInput reports feed the existing Sega joystick state, FPGA commands and OSD
hotkeys. XInput pads and mapped USB HID joysticks share the two USB ports
(`src/input/usb_joy`): a pad takes the first free port when it is plugged in,
the left one first, and keeps it until it is unplugged. A third pad is not
forwarded until a port is free. On-board joystick states are combined as
before. Unplugging a pad clears its buttons without moving the other pad to a
different port.

| XInput control | Karabas control |
| --- | --- |
| D-pad / left stick (50% dead zone) | Up, Down, Left, Right |
| A, B, X, Y | A, B, X, Y |
| RB or RT (>30) | C |
| LB or LT (>30) | Z |
| Start | Start |
| Back / Select | Mode |

Both shoulder and trigger inputs are accepted for C/Z to accommodate M30
mapping variants. This is a default mapping, not a hardware-verified claim
about every M30 revision. Guide and stick-click buttons are ignored.

## Hardware check

1. Flash the firmware and connect the M30 to the board's USB host port with
   a data cable. Follow the [M30 2.4G manual](https://download.8bitdo.com/Manual/Controller/M30-2.4g/M30_2.4g.pdf)
   for wired operation (the Bluetooth model's X+Start instructions do not apply).
2. Enable `debug=1` in `karabas.ini`. Look for `XInput mounted` with VID/PID
   and `type=3` (Xbox 360 wired).
3. Check directions, diagonals, A/B/C/X/Y/Z, Start and Mode in a core and OSD.
   The `XInput` log prints the button word and trigger values to diagnose mapping.
4. Hold a button and unplug the cable: the button must release. Reconnect and
   check again. Also test alongside a USB keyboard/mouse and, if available,
   a mapped HID joystick.

Wireless Xbox receivers and other Xbox controller families are outside the
validated target scope, even though the upstream driver recognizes them.

Target build: `ninja -C build karabas_minig`. Port tests:
`ctest --test-dir build-test -R test_usb_joy_slots`.
