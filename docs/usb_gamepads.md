# USB HID gamepads

[Русская версия](usb_gamepads.ru.md)

USB gamepads and joysticks reach the cores as a Sega MD2 pad (`CMD_JOYSTICK`): directions,
A B C X Y Z, Start and Mode. HID pads and [XInput pads](xinput.md) share the two USB ports.
A pad takes the first free port when it is plugged in and keeps it until it is unplugged.

A HID pad is mapped in one of two ways:

1. **Driver ini.** If `/drivers/` on the SD card has an ini with the VID/PID of the pad, the
   ini is used (format: [Driver ini](#driver-ini) below). It is made
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
   and how much the sticks jitter. A press in this time starts it again.
3. Press each MD2 button when asked and release it. `Enter` skips a button (it is also skipped
   after 10 s), `Backspace` goes back one.
4. `Enter` saves `/drivers/<vid>_<pid>.ini`. Other ini files for the same VID/PID are renamed to
   `*.ini.bak`. The drivers are reloaded, and the pad uses the new map right away. In the graphic
   setup the pad drawn on the screen then shows the buttons held through the new map.

For each press the mapper takes a hat nibble first, then a single bit, then the axis that moved
most, as an axis range from halfway between rest and pressed (`lib/common/hid_learn`).
The result is an ordinary driver ini that can be edited by hand.

## Driver ini

A driver is `/drivers/<anything>.ini` on the SD card; the OSD mapper names it `<vid>_<pid>.ini`.
Lines start with `;` for a comment and must fit in 79 characters.

```ini
[info]
; the VID / PID of the pad, hex, lower case
vid = 081f
pid = e401
; optional: a name for the log
name = Retrolink USB Gamepad
; optional: only the reports with this id (decimal), for pads that also send
; battery or vendor reports; without it every report of the pad is mapped
; report_id = 1

[buttons]
up = 1,0,0
down = 1,0,255
left = 0,0,0
right = 0,0,255
a = 5,1,5
b = 5,1,6
c = 6,1,0
x = 5,1,4
y = 5,1,7
z = 6,1,1
start = 6,1,5
mode = 6,1,4
```

Each of the 12 MD2 buttons (`up`, `down`, `left`, `right`, `a`, `b`, `c`, `x`, `y`, `z`, `start`,
`mode`) takes one or more sources separated by `|` (no spaces), each `pos,type,value[,arg]`.
`pos` is the byte in the report, from 0, after the report id byte if the pad has report ids.

| type | pressed when |
| --- | --- |
| 0 | byte `pos` equals `value` (0..255) |
| 1 | bit `value` (0..7) of byte `pos` is set |
| 2 | hat: the low 4 bits of byte `pos` shifted right by `arg` (0..4, default 0) are `value` (0..7, 0 = up, clockwise) or a diagonal next to it; 8..15 is centered |
| 3 | `value` <= byte `pos` <= `arg` (an axis range) |
| 4 | bit `value` of byte `pos` is clear |

More examples:

```ini
up = 1,3,0,63|5,2,0   ; stick up (byte 1 below 64) or hat up
right = 5,2,2,4       ; hat right, the hat in the high 4 bits of byte 5
start = 2,4,0         ; a button that reads 0 when pressed
```

Types 0 and 1 are the original format, so older ini files work unchanged. A driver ini holds
up to 32 sources. To find the bytes by hand on Linux: `lsusb` for the VID / PID, then
`sudo usbhid-dump -e stream -d <vid>:<pid>` and press the buttons one by one; or use the
[browser mapper](https://andykarpov.github.io/karabas-go-firmware/hidmapper/).

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
