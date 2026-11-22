# Karabas Go Firmware

[![Build](https://github.com/andykarpov/karabas-go-firmware/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/andykarpov/karabas-go-firmware/actions/workflows/build.yml)

English | [Русский](README.ru.md)

Firmware for the RP2040 (Raspberry Pi Pico) MCU of the Karabas Go family of FPGA retro computers.
The MCU is the companion of the FPGA. It loads the cores (bitstreams) from the SD card, draws the
on-screen menus, handles the keyboards, mice and joysticks and the real-time clock, and talks to
the running core over SPI.

**[USB gamepad mapper](https://andykarpov.github.io/karabas-go-firmware/hidmapper/)**: map a USB
HID gamepad in the browser and download its driver ini for `/drivers/` on the SD card. It is
rarely needed now: most pads work without an ini, and the board maps a pad itself (Setup >
Map USB gamepad in the OSD of a core, Setup > Gamepad in the boot menu), see
[USB HID gamepads](docs/usb_gamepads.md).

## Boards

| Board | Firmware in [karabas-go](https://github.com/andykarpov/karabas-go) | In a release of this repo |
| --- | --- | --- |
| Karabas Go | `firmware/karabas_go.uf2` | `karabas-go-firmware.uf2` |
| Karabas Mini (rev.C .. F3) | `firmware/karabas_mini.uf2` | `karabas-mini-firmware.uf2` |
| Karabas Mini G (rev.G / G2) | `firmware/karabas_minig.uf2` | `karabas-minig-firmware.uf2` |

The cores are `.kg` files, one per core for all the boards (`cores/` of karabas-go); the
firmware also reads the older per-board files `.kg1` / `.kg2` / `.kg3`.

## Features

- **Core loading.** The bitstream goes to the FPGA through PIO and DMA, then the ROMs of the
  core follow.
- **Boot menu.** The boot core (`boot.kg`, [karabas-go-core-menu](https://github.com/andykarpov/karabas-go-core-menu))
  draws the menu of the cores and its Setup itself, with its own CPU. It asks the firmware over
  the link ([docs/link_v2.md](docs/link_v2.md) section 5.9, RPC) for the core list, `karabas.ini`,
  the clock and the USB pads, and to start a core. A chosen core can start automatically after a
  timeout, once after power on. A `boot.kg` too old for this firmware (without RPC) gets a
  message from the firmware: put the new `boot.kg` on the card (BTN2 makes the card a USB drive).
- **OSD.** Cores with an on-screen menu get their options, switches and hotkeys from the core file.
  A long menu scrolls; a core can split it into sub-menus (pages, nested), separators and options
  shown or enabled only with a value of another option (the `core.json` keys are in
  [make_core](https://github.com/andykarpov/karabas-go-tools/blob/master/make_core/README.md#sub-menus-pages-and-conditional-items)).
  Disk images are mounted from the SD card and stay open while the core runs, with reads and
  writes. File loaders are supported too: their file list starts in the dir of the core and goes
  into the subdirs and anywhere on the card, it opens in the dir of the last file loaded (kept in
  `karabas.ini`); a file loader core with menu items opens them from its
  file list by Tab or O (Options), Esc goes back to the files. The file lists hide hidden and
  system entries and the names starting with `.`, and show the dirs first. On Karabas Go, physical floppy drives can be selected
  instead of an image.
- **Bitmap OSD.** Cores on link v2 ([docs/link_v2.md](docs/link_v2.md), all current cores) get a
  256x128 monochrome OSD drawn by the MCU (u8g2) with UTF-8 (Cyrillic) names: Off / On switches are
  check boxes (Space or Enter toggles them), Enter on a switch with more options opens the list of
  them, and Setup (clock, USB gamepad mapper) and About are the last entries of the main menu.
  Older cores keep the text OSD of link v1.
- **Input.**
  - USB keyboards (including NKRO) and mice.
  - USB HID gamepads: mapped by default from their HID descriptor, or with a driver ini
    learned in the OSD (or from the [browser mapper](https://andykarpov.github.io/karabas-go-firmware/hidmapper/)).
  - XInput pads (the Xbox 360 wired protocol, e.g. 8BitDo M30 2.4G).
  - PS/2 keyboards.
  - USB MIDI keyboards, to the cores that take MIDI.
  - Sega joysticks on the joystick ports (Karabas Go).
  - Every joystick reaches the core as a Sega Mega Drive 6-button pad.
- **USB host.** Runs on the second RP2040 core over PIO-USB, hubs included. A USB printer
  driver (Epson LX-300+II) is in progress.
- **Real-time clock**, set in Setup RTC of the OSD or on the Clock page of the boot menu (with
  the time zone and NTP sync), and passed to the core.
- **Wi-Fi** of the ESP8266: the boot core sets it up itself in its Setup (scan, join, Forget,
  Upgrade / Reset module) and syncs the clock over NTP; it takes the uart of the module while its
  Setup runs. The firmware bridges the uart of the module to the USB serial port
  (`AT+KARABAS=esp`).
- **SD card as a USB drive.** The PC sees the SD card as a USB mass storage device, so the card
  can stay in the board.
- **USB serial bridge.** Two USB serial ports: the control port (the log and the `AT+KARABAS=`
  commands that control the firmware) and the core serial port, the UART of the core byte for
  byte. A USB-UART adapter plugged into the USB host
  takes the place of the core serial port.
- **Optional peripherals**, detected at boot: OLED display, LED matrix, I2C extender (Go),
  ESP8266, MIDI (Mini G). The FT812 is reset before every core but the boot core; the cores that
  use it drive it themselves.
- **Fast transfers.** Link v2 cores take every message as one packet (a sector, a ROM block of up
  to 512 bytes) and raise an interrupt line when they have something for the MCU; older link v1
  cores that support it receive ROMs and files in bursts of up to 512 bytes over a second chip
  select.

## Getting the firmware

Each build of `master` that changes the firmware becomes a
[release](https://github.com/andykarpov/karabas-go-firmware/releases) with the three `.uf2` files.
The same files go to `firmware/` of [karabas-go](https://github.com/andykarpov/karabas-go), the
release repo of all the boards (with the cores): `karabas_go.uf2`, `karabas_mini.uf2`,
`karabas_minig.uf2`.

### Test firmware from a pull request

CI builds the firmware of every PR. Anyone signed in to GitHub can download it, no rights to
the repository are needed.

1. Open the PR page and go to the **Checks** tab.
2. Pick **Build** on the left, then **Summary** at the top of the left column.
3. At the bottom of the page, under **Artifacts**, download `firmware`.
4. Unzip it and flash the file of your board (`karabas_go.uf2`, `karabas_mini.uf2` or
   `karabas_minig.uf2`) as described in
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
| `/boot.kg` | the boot menu core ([karabas-go-core-menu](https://github.com/andykarpov/karabas-go-core-menu)), loaded at power on: the menu of the cores and its Setup |
| `/*.kg` | the cores: one file for all the boards (format kgo2, [docs/kgo2.md](https://github.com/andykarpov/karabas-go-tools/blob/master/docs/kgo2.md)), listed if the file has the board. The core file is only read: what you change in its menu is kept in `/config` |
| `/*.kg1` (`.kg2`, `.kg3`), `/boot.kg1` (...) | cores in the older per-board format kgo1, still read (`boot.kg` wins when both are there); their menu settings are written into the core file |
| `/config/<core id>.ini`, `.eep` | the settings of a `.kg` core: the options that differ from the core's defaults, the images, the last file of a file loader; its EEPROM. Made by the firmware; About (`A` in the core menu) > `R` deletes them |
| `/karabas.ini` | settings, optional, see below |
| `/drivers/*.ini` | USB gamepad drivers, optional ([format](docs/usb_gamepads.md#driver-ini)) |
| `<save_dir>/` or `<dir of the game>/saves/<game>.sav` | the backup RAM of the cores that have it (NES...), made by the firmware; the game is the file of a file loader or of an `FL` item (Doom: the WAD) |

The card can be FAT16, FAT32 or exFAT. The cores are built with
[make_core](https://github.com/andykarpov/karabas-go-tools/tree/master/make_core) from
[karabas-go-tools](https://github.com/andykarpov/karabas-go-tools).

### karabas.ini

The file is optional: without it the firmware uses the defaults below. Every key but `[fileloader]` and `[setup] watchdog`
can be changed in the Setup of the boot menu (sections System, Autoload, Display, Sound, Theme,
Clock), which reads and writes the file through the firmware. A save creates the file, or writes
only the changed keys and keeps the rest of it. Lines must be at most 78 characters long
(without the line end), or the whole file is ignored.

| Section | Key | Default | Meaning |
| --- | --- | --- | --- |
| `[setup]` | `debug` | `0` | `1`: debug log on the USB serial port |
| | `watchdog` | `1` | with `debug=1`: `0` turns off the hang watchdog (a reset after 8 s without progress of the main task, the cause in the next boot log) |
| | `dvi_only` | `0` | `1`: the cores drop the HDMI frames (audio, info) and output plain DVI |
| | `core_sort` | `name` | the order of the core list: `name` (the core name), `file` (the file name), `order` (the `order` of the core, then the name); the boot core is always first |
| | `tz` | `+00:00` | UTC offset for NTP sync, set on the Clock page of the boot menu, e.g. `+03:00`, `-04:30`; no daylight saving |
| `[ft812]` | | | the look of the boot menu (the section keeps the name of the FT812 menu it had) |
| | `sound` | `1` | the start sound of the menu: `0` off, `1` Klyuchik, `2` Ta-dan, `3` Run, `4` Sound 4 |
| | `click` | `1` | `1`: a click on a key press |
| | `time`, `date`, `char` | `1`, `1`, `1` | show the clock, the date and the Karabas picture in the menu |
| | `color_bg`, `color_gradient`, `color_button`, `color_active`, `color_text`, `color_text_active`, `color_copyright` | `010101`, `0000c8`, `202026`, `0000c8`, `ffffff`, `ffffff`, `787878` | colors (hex RGB) |
| | `mouse_speed` | `3` | mouse pointer speed in the menu, 1..5 |
| | `video_mode` | `0` | the video mode of the menu: `0` 640x480, `1` 800x600 (60 Hz VESA; another number is 640x480); Setup > Display > Video mode tries a mode and goes back after 15 s unless kept |
| `[matrix]` | `enabled`, `brightness`, `clock_*`, `meter_*` | `1`, `0`, ... | the LED matrix, see [docs/matrix.md](docs/matrix.md) |
| `[autoload]` | `enabled`, `timeout`, `core` | `0`, `15`, none | start the core with this id (`core.json` `id`, e.g. `tsconf`) after `timeout` seconds; once after power on, not after a return to the menu |
| `[fileloader]` | `<core id>` | none | the dir of the last file loaded by a file loader core in the older `.kgN` format (written by the firmware; empty or none: the dir of the core); a `.kg` core keeps it in `/config/<core id>.ini` |

Example:

```ini
[setup]
debug=0
tz=+03:00

[ft812]
sound=2

[autoload]
enabled=1
timeout=5
core=tsconf
```

## Controls

| Keys | Joystick | Action |
| --- | --- | --- |
| Menu (Win) + Esc | Start + C, Start + Select | open / close the OSD of the running core |
| Ctrl + Alt + Backspace | Start + X | back to the boot menu (the boot core is loaded, the MCU and its USB devices keep running, no autoload); in the boot menu: restart the MCU |
| arrows, Enter, Esc | D-pad, A / B, C or Select | navigate the menus (a held direction repeats); not in the gamepad mapping itself |
| Left | Left | back, where Left has nothing else to do: Setup menu, About |
| digits | Up / Down | the digit under the cursor of a date / time field |
| `S` (boot menu) | Start | the Setup of the boot menu |
| `S`, `A` (core OSD) | | Setup (clock, USB gamepad mapper), About |
| Space (core OSD, bitmap OSD) | | toggle a check box (an Off / On switch) |
| Enter, Right (core OSD) | A / B, Right | open the sub-menu of a `►` item |
| Esc (core OSD) | C or Select | back from a sub-menu to the menu above it; on the main menu (or the file list of a file loader core) it closes the OSD |
| Delete / Backspace (core OSD) | | eject the disk image of the selected drive |
| Backspace (file list) | | the parent dir |
| `H` (file list of a file loader core) | | back to the dir of the core |
| `R` (file list of a file loader core) | | read the list again |

Cores can define their own OSD hotkeys. In the boot menu the keys, the pads and the mouse go to
the menu of the boot core, which has its own keys.

## On-board buttons

Each core sets what the buttons **BTN1** and **BTN2** do (`buttons` in its `core.json`, see
`make_core` in karabas-go-tools): pass them to the core (reset, NMI... as the core decides; the
default), open the OSD, go back to the menu, reload the core, reboot into BOOTSEL or into USB mass
storage, on a press or on a hold. In the boot menu BTN1 reboots into BOOTSEL and BTN2 into USB mass
storage (a hold of 2 s: with the second SD card).

A reload of the core goes through the boot core, which fills the SRAM with zeros (0.2 s): the core
starts with clean memory, as from the menu.

## SD card as a USB drive

Press **BTN2** in the boot menu, or send `AT+KARABAS=msc` to the serial port. The board then shows
its SD card to the PC as a USB drive, and nothing else runs; the OSD says so over the boot menu.
Eject the drive on the PC to restart normally.

The second SD card (the slot of the FPGA, used by the cores) works the same way: hold **BTN2** in
the boot menu for 2 seconds, or send `AT+KARABAS=msc2`. The drive is named "SD Card 2" and is
slower (about 300 KB/s). Without a card in the slot the board goes back to the menu.

## USB serial bridge

### Two serial ports

The board shows two USB serial ports:

| Port | Linux (stable name) | What it is |
| --- | --- | --- |
| control (interface 0) | `/dev/serial/by-id/usb-Karabas_Go_<serial>-if00` | the debug log and the `AT+KARABAS=` commands below; not the UART of the core (since firmware 2026-11-19: use the core serial port) |
| core serial (interface 2, "Karabas core serial") | `/dev/serial/by-id/usb-Karabas_Go_<serial>-if02` | the UART of the core only, byte for byte: no commands, no log, no pause between bytes (a getty, a file transfer, a binary protocol) |

The `ttyACM` numbers follow the order the ports appear in (and other ACM devices), so use the
`by-id` names (or `by-path`, tied to the USB socket). A udev rule can give shorter names:

```
# /etc/udev/rules.d/60-karabas.rules
SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000a", ENV{ID_USB_INTERFACE_NUM}=="00", SYMLINK+="karabas-ctl"
SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000a", ENV{ID_USB_INTERFACE_NUM}=="02", SYMLINK+="karabas-serial"
```

On macOS the ports are `/dev/cu.usbmodem<serial>1` and `...3`, on Windows two COM ports (the second
one named "Karabas core serial" in the Device Manager). The 1200 baud open that restarts the board
into BOOTSEL works on the control port only.

On the control port, lines starting with `AT+KARABAS=` are answered by the firmware; its other
bytes are dropped (they no longer go to the UART of the core):

| Command | Action |
| --- | --- |
| `AT+KARABAS=list` | list the cores on the SD card with their builds |
| `AT+KARABAS=version` | `+KARABAS:<firmware>;<board>;<core id>;<core build>`: the firmware build, the board (1 Go, 2 Mini, 3 Mini G) and the running core |
| `AT+KARABAS=boot` | restart into BOOTSEL mode, to update the firmware |
| `AT+KARABAS=msc` | restart with the SD card as a USB drive |
| `AT+KARABAS=msc2` | restart with the second SD card (the slot of the FPGA) as a USB drive |
| `AT+KARABAS=sd2test` | boot core only: reads the second SD card at a few SPI clocks (a check of the card and the link) |
| `AT+KARABAS=esp` | boot core only: the bridge goes to the ESP8266, for its own AT commands (a CR, LF or CR LF from the terminal goes as CR LF) |
| `AT+KARABAS=uart` | back to the UART of the core |
| `AT+KARABAS=key:<keys>` | press and release keys as a USB keyboard would (core, OSD, hotkeys): keys apart by spaces, each modifiers and one key joined by `+` (`ctrl+alt+del`, `menu+esc`, `menu+f3`, `enter`, `f12`, `0x29` a HID usage), 100 ms down, 40 ms up |
| `AT+KARABAS=type:<text>` | type the text (printable ASCII, US layout) |
| `AT+KARABAS=hold:<key>` / `release` | keep a key down (until `release` or the next key) / all keys, mouse buttons and pad buttons up |
| `AT+KARABAS=sync` | `OK` once everything queued by `key`, `type`, `mouse`, `click` and `pad` has been delivered |
| `AT+KARABAS=mouse:<dx>,<dy>[,<buttons>[,<wheel>]]` | move as a USB mouse would (each axis up to 4000); buttons: bit 0 left, 1 right, 2 middle, left out or empty: the ones held stay |
| `AT+KARABAS=click[:left\|right\|middle]` | a mouse click (left by default) |
| `AT+KARABAS=pad:<port>:<buttons> ...` / `padhold:<port>:<buttons>` | press and release pad buttons on USB joystick port 0 or 1, e.g. `pad:0:up up+a` / keep them down until `release` or the next `pad`. Buttons: up down left right start a b c x y z mode |
| `AT+KARABAS=state` | `+KARABAS:core;<file>;<id>;<name>;<build>;<boot, osd, fileloader or hidden>`, `+KARABAS:osd;<open>;<screen>;<file browser open>` (screen `core_browser`: the boot core), then an `item` line per file item |
| `AT+KARABAS=load:<file or id>` | start a core from any core, `OK` when it has started; the boot core: loaded again, no MCU reboot |
| `AT+KARABAS=reset` | start the running core again with clean memory; in the boot core: the boot core loaded again, no MCU reboot |
| `AT+KARABAS=osd` / `osd:<item>` / `osd:<item>=<value>` | the menu items of the core: `+KARABAS:item;<pos>;<key>;<name>;<kind>;<enabled>;<value number>;<value>;<options or extensions>`; set by the number or the name of the option, a trigger with `=1` |
| `AT+KARABAS=mount:<item>:<path>` / `eject:<item>` | the image of a file item, saved like one chosen in the OSD. An item is its key or name (any case) or `#<pos>`; the argument of a line is at most 80 characters |
| `AT+KARABAS=debug:0` / `debug:1` | the debug log off / on until the next reboot; `karabas.ini` does not change |

### USB-UART adapter

A USB-UART adapter (CDC ACM, FTDI, CP210x or CH34x) plugged into the USB host of the board takes the
place of the core serial port while it is plugged: the bytes of the core go to it and its bytes to
the core, byte for byte. It runs 8N1 at the speed of the core: 115200 or the speed the core sets
(the DLL / DLM divisor of ZX Evo RS232). Only the first adapter is used. The core serial port gets nothing
meanwhile; the control port keeps the debug log and the `AT+KARABAS=` commands.

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
- [MCU ↔ FPGA link v2](docs/link_v2.md) ([по-русски](docs/link_v2.ru.md)): packets, targets,
  the INT line, the bitmap OSD; the link of all current cores.
- [MCU ↔ FPGA protocol, link v1](docs/protocol.md) ([по-русски](docs/protocol.ru.md)): SPI frames,
  all commands, load sequences; the link of older cores.
- [Burst transfers](docs/burst_protocol.md): the fast ROM / file transfer mode of link v1.
- [LED matrix add-on](docs/matrix.md).
- [Hardware checklist](docs/hw_checklist.md): what to check on a board before a risky change, with the baseline measurements.
- Core files (`.kg`, kgo2: [docs/kgo2.md](https://github.com/andykarpov/karabas-go-tools/blob/master/docs/kgo2.md); the older `.kg1` / `.kg2` / `.kg3`) and `core.json`:
  [karabas-go-tools/make_core](https://github.com/andykarpov/karabas-go-tools/tree/master/make_core).

## License

[MIT](LICENSE).
