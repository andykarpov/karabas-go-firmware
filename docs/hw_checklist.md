# Hardware checklist

[Русская версия](hw_checklist.ru.md)

What to check on a board before a risky change goes in: the rewrite (`docs/rewrite_plan.md`), a library bump, storage or USB work. Not every PR needs all of it: run the sections that the change can reach, and the **Smoke** section always.

Set `debug=1` in `karabas.ini` and keep a terminal on the USB serial port: most items are confirmed in the log. Mark the board in each run: **G** Karabas Go, **M** Mini, **MG** Mini G. Items for one board only say so.

## Smoke

- [ ] The board boots into the BOOT core, the core list shows (FT812 browser or text OSD).
- [ ] The log: `USB host: N hardware spinlocks free`, no `PIO programs did not load`, no `Last boot hung / panicked / halted`.
- [ ] `DIAG` every 10 s: core1 loops non-zero, `usb sof 1000/s` with a max gap of about 1000 us, `disc` / `own` / `ovf` 0, no task low on stack.
- [ ] A USB keyboard behind the hub works in the browser; a core loads and returns to BOOT (Ctrl+Alt+Backspace).

## Cores

- [ ] BOOT core: the core list, long names, the selection kept over a reboot.
- [ ] An OSD core (tsconf): loads, its ROMs arrive (`ROM ... ms` with the link mode in the log), Win+Esc opens and closes the OSD.
- [ ] A FILELOADER core (NES): the file list, a game loads, the last file is remembered (the id is written into the core file).
- [ ] A burst core (`FPGA caps` with burst in the log) and a classic one (old core, no caps answer).
- [ ] A core with its own `spi_freq`.
- [ ] The heaviest core (nanomig): loads, Kickstart autoload (`FL` item with `autoload`), runs.
- [ ] Autoload (`[autoload]`): the countdown, a key stops it.
- [ ] Ctrl+Alt+Backspace (joy Start+X) reboots into BOOT, also from inside the OSD.

## OSD

- [ ] Switches change the core at once, are saved into the core file and come back after a reload.
- [ ] Hotkeys of OSD items (Ctrl+Alt+Del of tsconf).
- [ ] A tree menu (nanomig): Enter / Right open a `►` page, Esc goes back, the cursor is kept per page, the path in the title row; a `show_if` item appears and an `enable_if` item turns grey with its switch; a hotkey of an item on another page opens that page. A flat core (tsconf) looks as before.
- [ ] Long lists: the scrollbar in the last column of the core browser and the file browser (a slot per page), the core menu and the Wi-Fi list (a thumb at most half the track); no scrollbar and no empty column when the list fits; the core menu scrolls with the cursor.
- [ ] `F` / `FL` browsers list the dirs (cyan, ending with `/`) and only the files with the item's extensions ("Files: ..." on the hint row).
- [ ] `F` slots: mount, eject, the browser starts in the dir of the last image (disk 2 after disk 1), Esc to the slot's own dir.
- [ ] The file mounter reads and writes (a disk image written by the core keeps its data after a reload).
- [ ] `.sav` (NES with `CAPS_BK`): created in `saves/`, kept after another game, another core, the reboot hotkey.
- [ ] `FL` ioctl loader: the file is sent, the log CRC-32 matches the file on a PC.
- [ ] **G:** physical floppy: `[ Physical DF0 ]` / `DF1` in an `F` slot with `physical`, the core reads the drive.

## Storage

- [ ] SD card FAT32 and exFAT: the lists, long names, opening by id.
- [ ] `karabas.ini` missing: defaults, the first save from Settings creates it. An invalid line (> 79 chars): `karabas.ini is invalid`, defaults.
- [ ] Settings save only writes the changed keys (`karabas.new` then rename), the comments stay.
- [ ] MSC (`msc` button action or `AT+KARABAS=msc`): the msc screen, the card on the PC, read / write speed, eject reboots (Linux eject, Windows "Safely Remove").
- [ ] MSC with the second card (`msc2`, BTN2 held 2 s in BOOT): "SD Card 2" on the PC, eject; no card: the screen says so, reboot after 5 s.
- [ ] `AT+KARABAS=sd2test` (BOOT core): reads at 4..24 MHz, 24 MHz may fail.
- [ ] No I/O errors in `journalctl -k` during MSC.

## Input

- [ ] USB keyboard and mouse, directly and behind a hub; the mouse in the FT menus.
- [ ] A low-speed device behind the hub (cheap gamepad, old keyboard): `own` stays 0.
- [ ] HID gamepads: with a `drivers/*.ini` and without one (the auto map); the two joystick ports keep their pads.
- [ ] Setup > Map USB gamepad: learn, save the ini, forget.
- [ ] XInput pad (8BitDo M30).
- [ ] PS/2 keyboard.
- [ ] **G:** Sega joysticks.
- [ ] On-board buttons: the actions of `core.btn` (press and hold), `bootsel`, `msc`, `msc2`.
- [ ] Keys are not lost under load (a core loading, the FT browser redrawing, debug log on).

## Printer

- [ ] USB printer (Epson LX-300): a page from a core prints whole, no repeated or lost lines (bulk OUT).

## Time and network (BOOT core)

- [ ] RTC: Setup RTC sets the date and time, kept over a power cycle.
- [ ] The RTC of the cores: MC146818A (profi on Go / Mini, pcxt: the DOS time) in BCD with an erased EEPROM (register B 02h), DS1307 (next), ZX Evolution (tsconf, profi on Mini G), MSM6242B (nanomig: the Workbench clock).
- [ ] Time zone `tz`: text Setup and the FT Clock page, saved.
- [ ] NTP sync sets the RTC.
- [ ] Wi-Fi: status, scan, password, join; the CTS flow control (`AT+UART_DEF`).
- [ ] Wi-Fi: Upgrade module (`AT+CIUPDATE`) and Reset module (`AT+RESTORE`) with the progress bar.

## Serial bridge

- [ ] The host's bytes reach the core UART and back.
- [ ] `AT+KARABAS=list`, `version`, `boot` (BOOTSEL), `msc`, `msc2`, `esp` / `uart` (BOOT core: the ESP AT console and back).
- [ ] `AT+KARABAS=key:menu+esc` opens the OSD, `key:esc` closes it; `type:dir` + `key:enter` in a core; `hold:alt` / `release`. (Mini G 2026-10-31: boot menu, PC XT OSD and file browser, Ctrl+Alt+Del driven this way.)
- [ ] A USB-UART adapter (FTDI, CP210x, CH34x, CDC ACM) on the USB host: `USB UART mounted` in the log, the core UART goes to it both ways at 115200 (tsconf ZiFi) and at the EVO RS232 speed; the CDC port keeps the log and `AT+KARABAS=`; unplugged, the bridge goes back to the CDC port; a second adapter is ignored.

## Displays

- [ ] **MG:** FT812 browser: the video modes (try, confirm, revert), the setup pages (System, Clock, Wi-Fi, Gamepad), sounds and the key click, colors.
- [ ] **MG:** the FT812 picture starts on every boot (no black screen, see `FT start` in the log).
- [ ] Text OSD browser with `[ft812] enabled=0`.
- [ ] LED matrix: `core` (the boot scroll, core meters), `scroll`, `effect` (clock, drive with the image slots), `image`, `blank`; `[matrix]` brightness and the clock / meter look; the icon in BOOTSEL and MSC; the progress bar while a core loads.
- [ ] OLED.

## Diagnostics

- [ ] The text OSD header shows the FPGA build and the firmware build in turn (3 s); `AT+KARABAS=version`.
- [ ] `debug=0`: no log, nothing slower. `debug=1`: the boot log is kept until a terminal opens the port.
- [ ] A CDC flood (a heavy log) with keys typed: no hang, no reset of the port.
- [ ] The watchdog: a hang reboots and the next boot logs `Last boot hung: ...`.

## Baseline

Measured on master before the rewrite, to compare against: 2026-10-27, master 3d1d797 (Pico-PIO-USB 0.7.3), Mini G, `debug=1`, the same SD cards.

| What | How | Value |
|---|---|---|
| Firmware size | `arm-none-eabi-size` of the minig elf | 618296 text, 12032 data, 140840 bss (3d1d797) |
| Boot to the BOOT core | the USB disconnect of the reboot to `Core type: Boot` in the log | ~1.6 s (after an MSC eject) |
| BOOT core bitstream | `Elapsed time` in the log | 504..506 ms (693096 bytes) |
| nanomig load | bitstream + ROMs + Kickstart | bitstream 619 ms (746600 bytes); Kickstart 512 KB in 1117 ms (burst); the core runs ~2.6 s after the choice. Booting from a 14 GB HDF: ~700 sector reads in 10 s, the longest wait 4.9 ms |
| File loader | a NES game, ms and KB/s in the log | NES bitstream 605 ms; a 256 KB game in 1093 ms (burst); `.sav` mounted |
| MSC SD1 read / write | `dd` with `iflag=direct` / `oflag=direct`, exFAT 32 GB | read 469 kB/s (64 MB), write 399 kB/s (32 MB) |
| MSC SD2 read | the same with `msc2`, mounted read-only (no writes to the second card) | read 375 kB/s (25 MB) |
| `DIAG` | loops per core, max gaps, heap, the lowest stacks, SOF | BOOT core, FT browser idle: core0 ~2200 loops/s, max gap 10..15 ms; core1 ~85000, max gap ~440 us; heap free 50080; stack free APP 3523, USB 1745, USBD 942, INPUT 817, MATRIX 902, OLED 928 words; SOF 1000/s, max gap ~1015 us, max frame ~210 us (low-speed pad behind the hub) |
| Host resets of the USB device port | `reset full-speed USB device` in `journalctl -k` | already there with `debug=1` before the rewrite: from a few to 200+ an hour, also once while nanomig booted from its HDF |
