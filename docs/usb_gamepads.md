# USB HID gamepads

USB gamepads and joysticks reach the cores as a Sega MD2 pad (`CMD_JOYSTICK`): directions,
A B C X Y Z, Start and Mode. HID pads and [XInput pads](xinput.md) share the two USB ports.
A pad takes the first free port when it is plugged in and keeps it until it is unplugged.

A HID pad is mapped in one of two ways:

1. **Driver ini.** If `/drivers/` on the SD card has an ini with the VID/PID of the pad, the
   ini is used (format: [`drivers/example_ini.txt`](../drivers/example_ini.txt)). It is made
   by the OSD mapper below, or by hand, or with the browser
   [hidmapper](https://andykarpov.github.io/karabas-go-firmware/hidmapper/) (source in
   [`tools/hidmapper`](../tools/hidmapper)), which is rarely needed now.
2. **Default map.** Otherwise the firmware builds a map from the HID report descriptor of the
   pad (`lib/common/hid_auto_map.c`). Most pads work this way without an ini.
   The default map is built for a pad with a driver too: it takes over when the driver is
   forgotten.

## OSD mapper

If the default map does not suit a pad, map it in the OSD: open Setup (`S` in the core browser
or in the core menu), then **Map USB gamepad**; in the graphic setup of the core browser it is
the **Gamepad** tab. A keyboard (or, in the graphic setup, a mouse) drives the screen.

The screen opens on the two joystick ports: the pad on each (VID:PID) and what maps it, its
driver ini, `default map` or `XInput`. **Forget** renames the driver ini of the chosen pad (all
of them for its VID/PID) to `*.ini.bak` and reloads the drivers: the pad goes on with its
default map at once. **Map** learns a pad:

1. Press a button on the pad. The first pad whose report changes is the one being mapped.
2. Release everything and leave the pad alone for a moment: the mapper learns the idle report
   and how much the sticks jitter.
3. Press each MD2 button when asked and release it. `Enter` skips a button (it is also skipped
   after 10 s), `Backspace` goes back one.
4. `Enter` saves `/drivers/<vid>_<pid>.ini`. Other ini files for the same VID/PID are renamed to
   `*.ini.bak`. The drivers are reloaded, and the pad uses the new map right away. In the graphic
   setup the pad drawn on the screen then shows the buttons held through the new map.

For each press the mapper takes a hat nibble first, then a single bit, then the axis that moved
most, as an axis range from halfway between rest and pressed (`lib/common/hid_learn`).
The result is an ordinary driver ini that can be edited by hand.

## Driver ini

The format is described in [`drivers/example_ini.txt`](../drivers/example_ini.txt). Each of the
12 MD2 buttons takes one or more sources separated by `|`, each `pos,type,value[,arg]`:

| type | pressed when |
| --- | --- |
| 0 | byte `pos` equals `value` |
| 1 | bit `value` of byte `pos` is set |
| 2 | hat: the low 4 bits of byte `pos` shifted right by `arg` are `value` (0 = up, clockwise) or a diagonal next to it |
| 3 | `value` <= byte `pos` <= `arg` (an axis range) |
| 4 | bit `value` of byte `pos` is clear |

Types 0 and 1 are the original format, so older ini files work unchanged. `report_id` in
`[info]` limits the driver to the reports with that id. Without it, every report of the pad is
mapped, as before. A driver ini holds up to 32 sources.

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

Set `debug=1` in `karabas.ini`. When a pad is plugged in, the log shows `uses its driver <file>`
and / or `HID default map: N fields`, and the resulting button word as `SEGA L` / `SEGA R`.

The raw reports of a pad are shown by the
[browser mapper](https://andykarpov.github.io/karabas-go-firmware/hidmapper/) (WebHID). Its
report descriptor can be read on a Linux PC with `usbhid-dump -d <vid>:<pid> -e descriptor`;
it is enough to add a test for a new pad to `test/test_hid_auto_map`.
