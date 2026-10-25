# USB HID gamepads

USB gamepads and joysticks reach the cores as a Sega MD2 pad (`CMD_JOYSTICK`): directions,
A B C X Y Z, Start and Mode. HID pads and [XInput pads](xinput.md) share the two USB ports.
A pad takes the first free port when it is plugged in and keeps it until it is unplugged.

A HID pad is mapped in one of two ways:

1. **Driver ini.** If `/drivers/` on the SD card has an ini with the VID/PID of the pad, the
   ini is used (format: [`drivers/example_ini.txt`](../drivers/example_ini.txt), made with the
   [hidmapper](https://karabas.uk/hidmapper/)).
2. **Default map.** Otherwise the firmware builds a map from the HID report descriptor of the
   pad (`lib/KarabasCommon/src/hid_auto_map.cpp`). Most pads work this way without an ini.

## Default map

Only the joystick / gamepad / multi-axis collections of the descriptor are used.

| HID | MD2 |
| --- | --- |
| first X / Y axis (left stick, or a d-pad reported as axes) | directions, beyond a quarter of the range from the center |
| hat switch | directions, with diagonals |
| D-pad up / down / right / left usages | directions |
| Button 1 / 2 / 3 / 4 | X / A / B / Y |
| Button 5 / 6, Button 7 / 8 | C / Z |
| Button 9, Select usage | Mode |
| Button 10, Start usage | Start |

The button order is the one of the common SNES / PlayStation style pads: 1 top, 2 right,
3 bottom, 4 left, then the shoulders, select and start. Pads that number their buttons
differently get a different layout. Write a driver ini for them.

Reports with other report ids (battery, vendor data) do not touch the buttons. Pads that only
have vendor-defined collections cannot be mapped by default and need a driver ini.

## Diagnostics

Set `debug=1` and `debug_hid=1` in `karabas.ini`. When a pad is plugged in, the log shows
`DESC <vid>:<pid>` with the report descriptor in hex, then `HID default map: N fields` or
`uses its driver ini`. Each report is logged as `RAW <vid>:<pid>: ...`, and the resulting
button word as `SEGA L` / `SEGA R`.

A descriptor from the log is enough to add a test for a new pad to
`test/test_hid_auto_map`.
