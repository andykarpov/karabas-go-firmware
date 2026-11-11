# MCU ↔ FPGA link v2 (draft)

[Русская версия](link_v2.ru.md) · Link v1: [protocol.md](protocol.md), [burst_protocol.md](burst_protocol.md)

Status: **draft, nothing implemented**. Link v2 is a second protocol on the same wires. It lives
next to v1: the core file says which link a core speaks, and the firmware keeps v1 for every core
that does not say v2.

## 1. Why

Link v2 takes its structure from the SPI protocol of the MiSTle FPGA Companion
([SPI.md](https://github.com/MiSTle-Dev/FPGA-Companion/blob/main/SPI.md), `mcu_spi.v` /
`sysctrl.v` / `osd_u8g2.v` of MiSTeryNano): one chip-select per message of any length, small
per-target state machines in the FPGA, an interrupt line from the FPGA with a status register,
and a monochrome bitmap OSD that the MCU draws. It is **not** wire-compatible with MiSTle: the codes,
the targets and the SPI mode are ours (the SD card of the Karabas boards is on the MCU, not on the FPGA).

What it fixes in v1:

| v1 | v2 |
|---|---|
| A 3-byte frame per data byte, chip select around every frame | One packet per message: a keyboard report is one 10-byte packet instead of 7 frames, a sector one packet of about 520 bytes |
| The FPGA can talk only in the reply bytes, so the firmware polls with NOP frames | The FPGA raises INT; the firmware reads the IRQ status register and serves the targets that asked |
| A reply queue of 512 × 24 bits in every core (`kg_mcu_link`) | Small per-target registers / FIFOs that the MCU reads on INT |
| Bursts on a second chip select with their own header | Every packet is a burst; no second chip select |
| Text OSD: a character / attribute grid and a font in the FPGA | A 256 × 128 monochrome bitmap; fonts and drawing in the MCU (u8g2) |

## 2. Physical layer

| Signal | Direction | v2 use |
|---|---|---|
| `MCU_SCK` | MCU → FPGA | SPI clock: 16 MHz by default, up to 32 MHz (section 9) |
| `MCU_MOSI` | MCU → FPGA | |
| `MCU_MISO` | FPGA → MCU | Driven by the FPGA only while `MCU_SS` is low |
| `MCU_SS` | MCU → FPGA | Packet select, active low. The only chip select of the link |
| `INT` | FPGA → MCU | Interrupt, **active high**, level |
| IO0 | (MCU → FPGA) | Reserved (the burst chip select of v1). The MCU keeps it an input; cores do not drive it |

INT is the line `BURST_READY` of v1, so it already goes from the FPGA to the MCU and the MCU input
has a pull-down: while the FPGA is configured, and with a v1 core, it reads 0 (no interrupt).

| Board | INT: RP2040 GPIO (firmware) | FPGA port in the core top | FPGA pin |
|---|---|---|---|
| Go | GPIO2 (`PIN_MCU_SPI_IO1`) | `MCU_SPI_IO[0]` | N9 |
| Mini | GPIO15 (`PIN_MCU_SPI_IO1`) | `MCU_IO[1]` | F6 |
| Mini G | GPIO15 (`PIN_MCU_SPI_IO1`) | `MCU_IO[1]` | F6 |

On Go the FPGA port index is swapped, as for the bursts of v1.

SPI mode 0 (CPOL 0, CPHA 0), MSB first, 8-bit bytes. (MiSTle uses mode 1; v2 keeps the mode of
`kg_spi_slave`.) The FT812 and SD2 chip selects stay as in v1 (`SPI_CONTROL`, section 5.1).

## 3. Packets

```
MCU_SS  ‾‾\________________________________________________/‾‾
MOSI       T    C    P0   P1  ...  Pn-1   00   00  ...  00
MISO       x    x    x    x   ...  x      x    D0  ...  Dm-1
```

- `T` is the target (section 4), `C` the command of that target, `P0..Pn-1` its parameters. A
  falling `MCU_SS` starts a new packet; every target forgets a half-received packet then. A rising
  `MCU_SS` ends it.
- A command that returns data has **one turnaround byte** after its last parameter (the MCU sends
  0, the FPGA output is undefined), then the data `D0..Dm-1` (the MCU sends 0). The turnaround gives
  the target one byte time to put `D0` into the shifter. After `D0` the target supplies its bytes
  from a register or a buffer it reads ahead, so a stream of 512 bytes runs without gaps.
- The MCU may end a packet early: the FPGA drops what was not sent. Reading past the end of the
  data gives 0.
- Multi-byte numbers are little-endian (as in v1).
- Unknown targets and commands are ignored until `MCU_SS` rises.
- A target never needs a gap between bytes. The MCU may leave gaps (byte-wise transfers); the
  FPGA must not depend on them.
- Commands with "repeated" parameters (`SETVAL`, `PS2`, `PUT`) take any number of entries in one
  packet.

Flow control is in band: a target that cannot take data reports it in a status command
(`LOAD STATUS`, `PORT STATUS`), and the MCU asks before it sends. There is no READY line.

## 4. Targets and interrupts

| T | Target | IRQ bit | Replaces in v1 |
|---|---|---|---|
| `00` | SYS: identity, interrupts, settings, board setup | 0 | `CMD_CAPS`, `HW_SETUP`, `SWITCHES`, `INIT_*`, `SPI_CONTROL`, `BTN`, `FLASHBOOT`, `DEBUG_*` |
| `01` | HID: keyboard, mouse, joysticks, PS/2 | 1 | `USB_KBD`, `USB_MOUSE`, `JOYSTICK`, `PS2_SCANCODE`, `MATRIX_BTN` |
| `02` | OSD: the bitmap overlay | 2 (unused) | `CMD_OSD` |
| `03` | IMG: mounted images, sectors, backup RAM, floppy drives | 3 | `40`..`4A`, `48`, `49` |
| `04` | LOAD: ROMs, file loader, ioctl loader | 4 | `ROM*`, `FILE*`, `IOCTL*`, bursts |
| `05` | PORT: byte streams (UART, ESP, MIDI, printer) | 5 | `UART`, `ESP_UART`, `MIDI`, `13`/`14` |
| `06` | RTC | 6 | `CMD_RTC` |
| `07` | AUX: LED matrix, audio peaks, debug values | 7 | `60`..`63`, `70`/`71`, `30`/`31` |

### Interrupt line

`INT = (pending != 0)`, `pending` being 8 bits, one per target. A target sets its bit while it has
something for the MCU (an event in its queue, data to read) and clears it itself when the MCU has
read it all. The MCU acknowledges only the edge-like causes of SYS (section 5.1).

Firmware: a GPIO level interrupt on INT (high) wakes the link task and is disabled; the task reads
`SYS IRQ`, serves each target whose bit is set, then enables the GPIO interrupt again. No NOP
polling. A slow safety read of `SYS IRQ` (about 100 ms) stays while v2 is new, and its finds are
logged with `debug=1`.

## 5. Commands

`→` = parameters from the MCU, `←` = data from the FPGA (after the turnaround byte).

### 5.1 SYS (`00`)

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `00` | `STATUS` | | `'K' 'G'`, version (2), caps (2 bytes), OSD width / 8, OSD height / 8, ports mask | The first packet after configuration; the magic says the core runs and speaks v2 |
| `01` | `IRQ` | ack mask | pending | `pending` as it was before the ack. Ack clears only the SYS causes (bit 0) |
| `02` | `IRQ_SRC` | | SYS causes | bit 0 coldboot, bit 1 reload request, bit 2 button read requested; reading clears bits 0 and 2 |
| `03` | `SETVAL` | (index, value) × n | | The OSD switches; index = the switch index of v1 (`sw` of kgo2), so pinned `index` values keep working. Triggers: 1, then 0 after 100 ms |
| `04` | `HW_SETUP` | key, value bytes | | key 0 board (`HW_ID`), 1 DVI only, `10` core build (8 chars), `18` firmware build (8 chars) |
| `05` | `INIT` | 1 = start (busy) / 0 = done | | As `CMD_INIT_START` / `CMD_INIT_DONE` |
| `06` | `SPI_CONTROL` | bits | | As `CMD_SPI_CONTROL`: bit 0 FT812 SPI, 1 FT812 video, 2 SD2, 3 ESP8266, 4 FT812 reset |
| `07` | `BTN` | button, state | | On-board buttons with the action `core` |
| `08` | `RELOAD` | | core id | Read after `IRQ_SRC` bit 1 (v1 `CMD_FLASHBOOT`) |

Caps (`STATUS` bytes 3..4): bit 0 IMG, 1 backup RAM, 2 write-protected images, 3 floppy drives
(DSKCHG), 4 LOAD flow control, 5 RTC writes, 6 AUX, 7 FT812 video, 8 reload. The ports mask says
which PORT indexes the core has (bit n = port n).

Coldboot: the SYS cause bit 0 is set when the link of the core leaves reset. The firmware sees it
after its own configuration (and clears it), and also when a core resets its link while it runs
(it reloaded itself, or crashed): the firmware then sends the start sequence again (section 6)
without loading the bitstream.

### 5.2 HID (`01`)

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `01` | `KBD` | 8 bytes: modifiers, 0, 6 key codes | | The USB boot report; one strobe per report for the core. Not sent while the OSD is open |
| `02` | `PS2` | (kind, code) × n | | kind 0 set 2, 1 XT; bit 7 of kind = release |
| `03` | `MOUSE` | dx, dy, wheel, buttons | | Signed deltas, as v1 |
| `04` | `JOY` | index, bits 7..0, bits 15..8 [, x, y] | | v1 bits; analog axes optional (0x80 centre) |
| `05` | `MATRIX_BTN` | button, state | | LED matrix add-on buttons |
| `06` | `EVENT` | | count n, then (command, argument) × n | PS/2 commands of the core (`ED` LEDs, `F0` set); IRQ bit 1 |

### 5.3 OSD (`02`)

The OSD is a monochrome bitmap of **256 × 128 pixels** (4096 bytes), in the page layout of u8g2
and small OLEDs: 16 pages of 8 lines; in a page, byte x holds the pixels of column x, bit 0 at the
top. The size comes from `STATUS` (bytes 5..6), so a core with less block RAM may offer 128 × 64.

| C | Name | → | Notes |
|---|---|---|---|
| `01` | `ENABLE` | bit 0 show | |
| `02` | `WRITE` | tile (2 bytes, LE), data bytes | tile = page × (width / 8) + column / 8; bytes go on from `tile × 8`, wrapping at the end. u8g2 sends a tile row at a time |
| `03` | `COLORS` | fg R G B, bg R G B, bg alpha (0..3) | Defaults: white on dark blue, alpha 2 |
| `04` | `POPUP` | x, y, w, h (in tiles), place, zoom | Show only this part of the bitmap, as a popup; w = 0 ends the popup (the whole bitmap again). place: 0 centre, 1 top, 2 bottom of the screen. zoom: 0 the scale of the OSD, 1 twice that (as the large cells of the v1 popup) |

The core scales the bitmap by an integer factor that fits its video mode (×2 for 640 / 800 wide,
×3 or ×4 for 1024 × 768 and more), centres it, and draws a border and a shadow as `osd_u8g2.v`.
A popup gets the same border and shadow around its window. `ENABLE` and `POPUP` are independent
as in v1 (show and popup): the OSD is visible while either is on; with both on, the window is shown.

Monochrome by design: one colour pair for the whole OSD (v1 had 16 ink / paper colours per cell).
Emphasis is drawn (inverse bars, frames, icons, fonts). No colour planes are planned.

#### Popups and dialogs

What v1 does with the OSD and how it maps onto v2:

| v1 | Where | v2 |
|---|---|---|
| Popup mode: the cells twice as large while the menu is hidden; the hotkey toast (logo, item name and value, 500 ms) | `hotkey_popup()`, `ui_popup_start()`, `osd_show_popup()` | The MCU draws the toast into a region of the bitmap and shows it with `POPUP` (zoom 1, place top or centre); it hides it with `POPUP w = 0` and `ENABLE 0` after 500 ms. Keys keep going to the core |
| Progress popup over the hidden menu: "Loading..." of the file loaders, "Saving..." of the backup RAM, "Run SAV" | `osd_progress_popup()`, `osd_loading_popup()` | A progress dialog (below) shown with `POPUP` (zoom 0, centre) |
| Progress over the open menu | the same functions, drawn into the menu cells | The same dialog drawn over the menu in the MCU frame buffer; no `POPUP` |
| Popup number for the core's own overlay (`ADDR_POPUP` bits 7..4) | cores with their own popup graphics | Dropped: every popup is drawn by the MCU |

The firmware keeps the menu screen in its own frame buffer and draws popups and dialogs in a second
layer; after a dialog it sends the tiles it covered again. Progress updates send only the tile row
of the bar (one 8-line page, at most 256 bytes: under 0.1 ms at 32 MHz), so a bar can move at
every 512-byte sector without slowing the transfer.

Dialog kinds of the UI layer (section 8), the same for v1 (text cells) and v2 (bitmap):

- **toast**: one or two lines, a timeout, takes no keys (hotkey switch values, "Disk A: game.adf");
- **progress**: title, a line of text, a bar with a percentage, optional bytes / time left; takes
  no keys, except Esc where the operation can stop (file loading);
- **busy**: title and a running indicator when the length is unknown (waiting for a core, Wi-Fi);
- **message / confirm**: a text and OK, or Yes / No (reset settings, overwrite, errors), modal:
  the keys go to the dialog.

### 5.4 IMG (`03`)

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `01` | `MOUNT` | slot, size (8 bytes), flags | | size 0 = eject. Flags: bit 0 write protected, bit 1 / 2 physical drive DS0 / DS1 (Go) |
| `02` | `EVENT` | | kind, slot, arguments | One event per read, IRQ bit 3 while more wait. Kinds below |
| `03` | `RDATA` | slot, 512 bytes, sum (2) | status | Sector of a read request; status 0 taken, 1 bad sum (the core asks again) |
| `04` | `WDATA` | slot | 512 bytes, sum (2) | Sector of a write request, from the buffer of the core |
| `05` | `DONE` | slot, status | | 1 done, 2 error, 3 write protected (data dropped) |
| `06` | `FLUSH` | slot | | Backup RAM: write the dirty sectors now; the answer comes as an event |
| `07` | `FDD_CHNG` | drive, value | | Answer to the DSKCHG event: 1 changed / no disk, 0 disk in, 2 no line |

Events (`EVENT` kind):

| kind | Meaning | Arguments |
|---|---|---|
| 0 | none | |
| 1 | read request | LBA (4) |
| 2 | write request (the sector waits in the core: `WDATA`) | LBA (4) |
| 3 | flush finished | 1 no dirty sectors left / 2 a write failed |
| 4 | backup RAM mount | size in sectors (4), file kind (0 `.sav`) |
| 5 | DSKCHG request (the core selects the drive until `FDD_CHNG`) | drive (1 DS0, 2 DS1) |

One request per slot at a time, as in v1; the core repeats a request after 200 ms without `DONE`.
`img_sdc`, `hdf_ata`, `kg_bk_ram` keep their core-side ports.

### 5.5 LOAD (`04`)

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `01` | `START` | kind, slot, size (8), extension (ASCII, up to 8) | | kind 0 ROM (the core holds its CPU in reset), 1 file loader (reset + active), 2 ioctl (`FL` item) |
| `02` | `DATA` | address (4), 1..512 bytes | | Byte k to address + k |
| `03` | `END` | kind | | |
| `04` | `STATUS` | | free (512-byte blocks), 0 = wait | Only with caps bit 4; without it the core takes `DATA` at any rate |

With caps bit 4 the MCU reads `STATUS` before a `DATA` packet when its last answer is used up, and
waits on IRQ bit 4 (set when space comes back) or polls every 1 ms, 100 ms at most.

### 5.6 PORT (`05`)

Byte streams, port index = `P0` of every command.

| Port | Stream |
|---|---|
| 0 | USB serial port / USB-UART adapter (v1 `CMD_UART`: ZiFi, EVO RS232) |
| 1 | ESP8266 module behind the FPGA (v1 `CMD_ESP_UART`) |
| 2 | MIDI IN from a USB MIDI keyboard (MCU → FPGA only, v1 `CMD_MIDI`) |
| 3 | USB printer (FPGA → MCU only) |

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `01` | `STATUS` | port | rx free, tx available, config (4) | config: port 0 the divisor DLL, DLM (0 = 115200), port 3 status request flag |
| `02` | `PUT` | port, bytes | | MCU → core; at most rx free |
| `03` | `GET` | port, count | count bytes | core → MCU |
| `04` | `PSTAT` | port, status | | Status into the core: port 3 printer (bit 7 BUSY) |

IRQ bit 5 while any port has tx bytes.

### 5.7 RTC (`06`)

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `01` | `SET` | first register, values | | All registers at start, the time registers every 500 ms (formats of v1 `rtc_mode`) |
| `02` | `GET` | | count n, (register, value) × n | Registers the CPU of the core wrote; IRQ bit 6 |

### 5.8 AUX (`07`)

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `01` | `EVENT` | | count n, (cmd, addr, data) × n | The v1 frames of the core as they are: `60`..`62` matrix, `70` / `71` peaks, `30` / `31` debug; IRQ bit 7 |

## 6. Core start

1. Bitstream through the configuration pins.
2. `SYS STATUS` until it answers `'K' 'G' 2` (100 ms at most; INT may already be high from the
   coldboot cause). No answer: the core is reported as not running.
3. `SYS IRQ_SRC` (clears coldboot), `SYS INIT 1`.
4. `SYS HW_SETUP`: board, DVI only, the two build ids.
5. ROMs: `LOAD START 0`, `DATA`…, `END 0`.
6. `SYS SETVAL` with all switches in one packet, `RTC SET` with all registers.
7. `HID KBD` with an empty report, `IMG MOUNT` of the saved images, `FL` autoload.
8. `SYS INIT 0`. The GPIO interrupt on INT is enabled.

There is no font download (the font is in the MCU) and no caps query apart from `STATUS`.

## 7. Choosing the link: core.json and kgo2

- `core.json`: `"link": 2` at the top level or per board (`boards.minig.link`), default 1.
- kgo2: the CORE record gets the field `link` (offset 16, 1 byte; the record grows, older readers
  skip it). A v2 bitstream goes into a new section type **10 `BITSTREAM_LINK2`**, not into 9.
  An older firmware then finds no bitstream for its board and treats the file as no core for this
  board, instead of loading it and talking v1 to it. make_core writes type 10 for a board with
  link 2 and refuses `link` 2 for a `.kg1/.kg2/.kg3` output.
- Strings of the menu are UTF-8 (Cyrillic names); a v1 core keeps ASCII / CP866 as now.

## 8. OSD drawing in the firmware

- u8g2 (BSD 2-clause) in `third_party/u8g2`, a full-buffer display of 256 × 128 (or the size of
  `STATUS`) in MCU RAM (4 KB). The firmware sends only the tile rows that changed.
- A display layer under the menu code: the text grid backend of v1 (`lib/osd`, `osd_ui`) and the
  u8g2 backend of v2 share the menu, the file browser and the dialogs (toast, progress, busy,
  message / confirm, section 5.3): the callers (`hotkey_popup()`, the loaders, `bk_save`,
  `uknc_sav`) ask for a dialog, not for cells. The OLED of the backlog can reuse the u8g2 backend.
- The header with the logo, the build ids and the footer become graphics (a small bitmap logo,
  not font characters).
- New fonts with Cyrillic, from u8g2: `u8g2_font_6x12_t_cyrillic` for the menu (42 columns, 10
  lines), `u8g2_font_5x8_t_cyrillic` for long lists and the status line (51 × 16),
  `u8g2_font_haxrcorp4089_t_cyrillic` as a proportional option. Titles and icons are drawn as
  graphics. The CP866 font of the text OSD and its logo characters are not used in v2.

## 9. FPGA side (karabas-go-modules)

- `kg_l2_spi`: the byte shifter on `MCU_SCK` (mode 0, the chip-select deglitch of `kg_mcu_link`),
  a toggle into the core clock, the next MISO byte requested a few bits ahead.
- `kg_l2_bus`: target / command decode, `strobe`, `start`, `dout` to the targets, `din` back.
- Targets `kg_l2_sys`, `_hid`, `_osd`, `_img`, `_load`, `_port`, `_rtc`, `_aux`, each behind an
  `ENABLE_*` parameter.
- `kg_mcu2`: the wrapper with **the same core-side ports as `kg_mcu`** plus `INT`, so a core moves
  to v2 by swapping the module, the INT pin in the ucf and `"link": 2`.
- Clock: 32 MHz SCK with a core clock of about 28 MHz must work (the bursts of v1 do); the exact
  bound comes from the simulation of `kg_l2_spi` and goes here.
- Expected saving per core: the reply queue (512 × 24) and `kg_burst_rx` go, the OSD font RAM goes;
  the OSD bitmap needs 4 KB (2 RAMB16). Measured on the first cores.

## 10. Plan

One branch and one PR per phase and repository.

| Phase | Work |
|---|---|
| Ф0 | This specification |
| Ф1 | Firmware refactor without a change of behaviour: the services (input, OSD, img, loaders, RTC, ports, matrix) call a link interface instead of building frames, and the popups go through the dialog API (section 5.3) on the text backend; v1 byte for byte, checked on the board |
| Ф2 | Transport + SYS / HID / LOAD: `kg_l2_spi`, `kg_l2_bus`, `kg_mcu2` with a verilator testbench and an MCU model; firmware `link2` (DMA for long packets, the INT task); make_core `link` + section 10. Pilot cores on Mini G: the **boot core** and **vic20** |
| Ф3 | OSD: `kg_l2_osd` (bitmap, `POPUP` windows with zoom), u8g2 and the display layer, the Cyrillic fonts, the menu and the dialogs in graphics |
| Ф4 | IMG: mounter, `.sav`, write protection, physical floppy drives |
| Ф5 | PORT, RTC, AUX, `SPI_CONTROL` / FT812 |
| Ф6 | The other cores one by one (each its own PR), the ports of MiSTle cores first (atarist, macplus, atari2600) |

The firmware keeps link v1 for every core that is not moved.
