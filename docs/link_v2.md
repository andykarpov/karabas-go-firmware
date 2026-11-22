# MCU ↔ FPGA link v2

[Русская версия](link_v2.ru.md) · Link v1: [protocol.md](protocol.md), [burst_protocol.md](burst_protocol.md)

Status: **released** (firmware v26111315, 2026-11-13; all cores of the release on it on Go, Mini
and Mini G since 2026-11-14). The core side is in karabas-go-modules (`kg_mcu2`, `kg_l2_*`, guide
`docs/link_v2_migration.md`), the core file key `"link": 2` in karabas-go-tools `make_core`. Link v2
is a second protocol on the same wires. It lives next to v1: the core file says which link a core
speaks, and the firmware keeps v1 for every core that does not say v2.

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
| `MCU_SCK` | MCU → FPGA | SPI clock: the core's `spi_freq`, 16 MHz requested by default. SPI0 divides 48 MHz, so the wire runs at 12 MHz by default and at 24 MHz at most (a 32 MHz request); the cores are simulated up to 32 MHz (section 9) |
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
- Commands with "repeated" parameters (`SETVAL`, `PS2`, `JOY`, `PUT`) take any number of entries
  in one packet. The firmware now sends one entry per packet; only the build ids (`HW_SETUP`) and
  the runs of `RTC SET` are batched.

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
| `08` | RPC: the calls of the core to the firmware | 8 | new |

### Interrupt line

`INT = (pending != 0)`, `pending` being 16 bits, one per target (`SYS IRQ` answers them in two bytes). A target sets its bit while it has
something for the MCU (an event in its queue, data to read) and clears it itself when the MCU has
read it all. The MCU acknowledges only the edge-like causes of SYS (section 5.1).

Firmware: the `APP` main loop reads the INT pin on every pass (`link_poll()`; no GPIO interrupt,
no task of its own). While it is high, `link_v2_service()` reads `SYS IRQ` and serves each target
whose bit is set (SYS, IMG one event per pass, PORT, RTC, AUX, HID). No NOP polling; nothing is
read while INT is low.

## 5. Commands

`→` = parameters from the MCU, `←` = data from the FPGA (after the turnaround byte).

### 5.1 SYS (`00`)

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `00` | `STATUS` | | `'K' 'G'`, version (2), caps (2 bytes), OSD width / 8, OSD height / 8, ports mask | The first packet after configuration; the magic says the core runs and speaks v2 |
| `01` | `IRQ` | ack mask | pending 7..0, pending 15..8 | `pending` as it was before the ack. Ack clears only the SYS causes (bit 0) |
| `02` | `IRQ_SRC` | | SYS causes | bit 0 coldboot, bit 1 reload request; reading clears bit 0 |
| `03` | `SETVAL` | (index, value) × n | | The OSD switches (the firmware sends one pair per packet); index = the switch index of v1 (`sw` of kgo2), so pinned `index` values keep working. Triggers: 1, then 0 after 100 ms |
| `04` | `HW_SETUP` | key, value bytes | | value byte k is key + k: 0 board (`HW_ID`), 1 DVI only, `10`..`17` core build, `18`..`1F` firmware build. The firmware sends board and DVI as two packets and the 16 build characters in one |
| `05` | `INIT` | 1 = start (busy) / 0 = done | | As `CMD_INIT_START` / `CMD_INIT_DONE` |
| `06` | `SPI_CONTROL` | bits | | As `CMD_SPI_CONTROL`: bit 0 FT812 SPI, 1 FT812 video, 2 SD2, 3 ESP8266, 4 FT812 reset |
| `07` | `BTN` | button, state | | 0, 1 the on-board buttons with the action `core`; 2..5 the buttons of the LED matrix add-on (v1 `CMD_MATRIX_BTN`) |
| `08` | `RELOAD` | | core id | Read after `IRQ_SRC` bit 1 (v1 `CMD_FLASHBOOT`); `FE` from any core = a load fault (a part of the ROMs lost): the firmware loads the core again, 2 times in a row at most |

Caps (`STATUS` bytes 3..4): bit 0 IMG, 1 backup RAM, 2 write-protected images, 3 floppy drives
(DSKCHG), 4 LOAD flow control (`LD_READY`), 5 RTC, 6 AUX, 7 FT812 video, 8 reload, 9 MIDI IN, 10 RPC.
The firmware uses bits 1, 2, 4, 5 (`RTC SET` is sent only with it) and 9; the others are
informational (AUX is served on IRQ bit 7 whatever the caps say). The ports mask says which PORT
indexes the core has (bit n = port n); the firmware sends nothing to a port the core does not have.

Coldboot: the SYS cause bit 0 is set when the link of the core leaves reset. The firmware sees it
after its own configuration (and clears it), and also when a core resets its link while it runs
(it reloaded itself, or crashed): the firmware then only logs it (`debug=1`), it does not send the
start sequence again.

### 5.2 HID (`01`)

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `01` | `KBD` | 8 bytes: modifiers, 0, 6 key codes | | The USB boot report; one strobe per report for the core (`KBD_WR`). Not sent while the OSD is open |
| `02` | `PS2` | (kind, code) × n | | kind 0 a set 2 byte, 1 an XT byte (bit 7 of the code = release): a strobe per byte (`PS2_WR`), the firmware sends one byte per packet |
| `03` | `MOUSE` | dx, dy, wheel, buttons | | Signed deltas; one strobe per report (`MS_WR`) |
| `04` | `JOY` | (port, bits 7..0, bits 15..8) × n | | v1 bits; a strobe per entry (`JOY_WR`); the firmware sends one port per packet |
| `06` | `EVENT` | | count n, then (command, argument) × n | PS/2 commands of the core to the keyboard (`ED` LEDs, `F0` set); IRQ bit 1 |

### 5.3 OSD (`02`)

The OSD is a monochrome bitmap of **256 × 128 pixels** (4096 bytes), in the page layout of u8g2
and small OLEDs: 16 pages of 8 lines; in a page, byte x holds the pixels of column x, bit 0 at the
top. The size comes from `STATUS` (bytes 5..6). The firmware supports only 256 × 128: a core that
reports another size gets no OSD (no `ENABLE`, `WRITE` or `POPUP`).

| C | Name | → | Notes |
|---|---|---|---|
| `01` | `ENABLE` | bit 0 show | |
| `02` | `WRITE` | tile (2 bytes, LE), data bytes | tile = page × (width / 8) + column / 8; bytes go on from `tile × 8`, wrapping at the end. u8g2 sends a tile row at a time |
| `03` | `COLORS` | fg R G B, bg R G B, bg alpha: 0 none, 1 a half, 2 three quarters, 3 opaque | Defaults: white on dark blue, alpha 2 (the firmware sets 3) |
| `04` | `POPUP` | x, y, w, h (in tiles), place, zoom | Show only this part of the bitmap, as a popup; w = 0 ends the popup (the whole bitmap again). place: 0 centre, 1 top, 2 bottom of the screen. zoom: 0 the scale of the OSD, 1 twice that (as the large cells of the v1 popup) |

The core scales the bitmap by an integer factor that fits its video mode (×2 for 640 / 800 wide,
×3 or ×4 for 1024 × 768 and more), centres it, and draws a border and a shadow as `osd_u8g2.v`.
A popup gets the same border and shadow around its window. `ENABLE` and `POPUP` are independent
as in v1 (show and popup): the OSD is visible while either is on; with both on, the window is shown.

Monochrome by design: one colour pair for the whole OSD (v1 had 16 ink / paper colours per cell).
Emphasis is drawn (inverse bars, frames, icons, fonts). No colour planes.

#### Popups and dialogs

What v1 does with the OSD and how it maps onto v2:

| v1 | Where | v2 |
|---|---|---|
| Popup mode: the cells twice as large while the menu is hidden; the hotkey toast (logo, item name and value, 500 ms) | `hotkey_popup()`, `ui_popup_start()`, `osd_show_popup()` | The MCU draws the toast into a region of the bitmap and shows it with `POPUP` (zoom 1, centre); it hides it with `POPUP w = 0` and `ENABLE 0` after 500 ms. Keys keep going to the core |
| Progress popup over the hidden menu: "Loading..." of the file loaders, "Saving..." of the backup RAM, "Run SAV" | `osd_progress_popup()`, `osd_loading_popup()` | A progress dialog (below) shown with `POPUP` (zoom 0, centre) |
| Progress over the open menu | the same functions, drawn into the menu cells | The same dialog drawn over the menu in the MCU frame buffer; no `POPUP` |
| Popup number for the core's own overlay (`ADDR_POPUP` bits 7..4) | cores with their own popup graphics | Dropped: every popup is drawn by the MCU |

The firmware keeps one frame buffer (`ui/osd2`) and a copy of what the core has; each flush sends
only the runs of changed 8-byte tiles, so a progress update is a few tiles of the bar (a whole
8-line page is 256 bytes, about 0.1 ms at 24 MHz) and a bar can move at every 512-byte sector
without slowing the transfer. Dialogs draw into the same buffer; the screen under them is redrawn
afterwards.

Dialog kinds of the UI layer (`ui/dialog`, section 8), the same for v1 (text cells) and v2 (bitmap):

- **toast** (`dialog_toast()`): two lines, 500 ms, takes no keys (hotkey switch values);
- **progress** (`dialog_progress()`, `dialog_loading()`): a title and a bar; takes no keys;
- **box** (`dialog_box()`): a title and a line in a frame, kinds ERROR, BUSY (waiting for a core,
  Wi-Fi) and ASK; no buttons: for ASK the caller reads Enter / Esc;
- **load screen** (`dialog_load_screen()`, `dialog_load_rom*()`): the ROM loading of a core.

### 5.4 IMG (`03`)

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `01` | `MOUNT` | slot, size (8 bytes), flags | | size 0 = eject. Flags: bit 0 write protected, bit 1 / 2 physical drive DS0 / DS1 (Go) |
| `02` | `EVENT` | | kind, slot, arguments (5) | Z first; one event per read (the read of `kind` takes it), IRQ bit 3 while more wait. Kinds below |
| `03` | `RDATA` | slot, 512 bytes, sum (2) | status | Z before the status. Sector of a read request; status 0 taken, 1 refused (bad sum, no read waiting): the mcu sends it again |
| `04` | `WDATA` | slot | 512 bytes, sum (2) | Z after the slot. Sector of a write request, from the buffer of the core |
| `05` | `DONE` | slot, status | | 1 done, 2 error, 3 write protected (data dropped) |
| `06` | `FLUSH` | slot | | Backup RAM: write the dirty sectors now; the answer comes as an event |
| `07` | `FDD_CHNG` | drive, value | | Answer to the DSKCHG event: 1 changed / no disk, 0 disk in, 2 no line |

Events (`EVENT` kind):

| kind | Meaning | Arguments |
|---|---|---|
| 0 | none | |
| 1 | read request | LBA (4, LE) |
| 2 | write request (the sector waits in the core: `WDATA`) | LBA (4, LE) |
| 3 | flush finished | 1 no dirty sectors left / 2 a write failed |
| 4 | backup RAM mount | size in sectors (4), file kind (0 `.sav`) |
| 5 | DSKCHG request (the core selects the drive until `FDD_CHNG`) | drive (1 DS0, 2 DS1) |

One request per slot at a time, as in v1; the core repeats a request after 200 ms without `DONE`.
The sector interface of `img_sdc` (`sdc_*`) stays for the drives of the cores; its link side is
new (`kg_l2_img`), and `kg_bk_ram`, `hdf_ata` sit on it as now (section 11).

### 5.5 LOAD (`04`)

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `01` | `START` | kind, slot, size (8), extension (ASCII, up to 8) | | kind 0 ROM (the core holds its CPU in reset), 1 file loader, 2 `FL` item; `LD_START` one clock after the size (a new file: the core resets what it loads into), `LD_ACTIVE` until `END` |
| `02` | `DATA` | kind, address (4), 1..512 bytes | | Byte k to address + k (`LD_WR` per byte); a packet of another kind than the open `START` is dropped |
| `03` | `END` | kind | | `LD_ACTIVE` off, `LD_END` one clock |
| `04` | `STATUS` | | 1 = a `DATA` packet of 512 bytes may come (`LD_READY`), 0 = wait | Only with caps bit 4; without it the core takes `DATA` at any rate (a byte per 333 ns at 24 MHz) |

With caps bit 4 the MCU reads `STATUS` before each `DATA` packet and polls it every 1 ms while it
is 0, 100 ms at most.

### 5.6 PORT (`05`)

Byte streams, port index = `P0` of every command.

| Port | Stream |
|---|---|
| 0 | the core UART: the core serial port CDC 1, or a USB-UART adapter on the USB host (v1 `CMD_UART`: ZiFi, EVO RS232) |
| 1 | ESP8266 module behind the FPGA (v1 `CMD_ESP_UART`) |
| 2 | MIDI IN from a USB MIDI keyboard (MCU → FPGA only, v1 `CMD_MIDI`; no byte counter: a byte the core has no room for within 10 ms is dropped, not counted) |
| 3 | USB printer (FPGA → MCU only) |

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `01` | `STATUS` | port | rx free, tx available, config (4) | Z after the port; both counts at most 255. config: port 0 the divisor DLL, DLM (0 = 115200) |
| `02` | `PUT` | port, bytes | | MCU → core; at most rx free (the firmware sends one byte per packet) |
| `03` | `GET` | port, count | count bytes | Z after the count; core → MCU (00 past what waits) |
| `04` | `PSTAT` | port, status | | Status into the core: port 3 printer (bit 7 BUSY), sent after every GET of port 3 |

IRQ bit 5 while any port has tx bytes.

### 5.7 RTC (`06`)

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `01` | `SET` | first register, values | | Register first + k = value k. All registers at start, the time registers every 500 ms (formats of v1 `rtc_mode`); the firmware sends each run of consecutive registers as one packet |
| `02` | `GET` | | count n, (register, value) × n | Z first. Registers the CPU of the core wrote (a fifo of 8, not while busy, not C, nor D with SKIP_0D); IRQ bit 6 |

### 5.8 AUX (`07`)

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `01` | `MATRIX` | | control, 32 bitplane bytes | Z first. The picture of the core for the LED matrix (bytes 0..7 the red rows of the left matrix, 8..15 its green, 16..31 the right one; control: bit 2 brightness in bits 7..4, bit 3 meter mode). The core draws into its own bitplanes (pixels or rows), the MCU reads them whole when they changed |
| `02` | `PEAKS` | | left (2), right (2) | Z first, LE. The highest audio peaks since the last read (it clears them), for the matrix meters |
| `03` | `DEBUG` | | addr (2), data (2) | Z first, LE. Debug values of the core for the log, when they changed |
| `04` | `CHANGED` | | bits | Z first. Bit 0 the matrix (an update, a new mode or brightness), 1 new peaks, 2 the debug values; the read of `MATRIX` / `PEAKS` / `DEBUG` clears its bit |

IRQ bit 7 while one of them changed since it was read. The core keeps the frames of v1 (`MATRIX_CMD`: CTL, PIXEL, BITPLAN, the peaks of `audio_peaks.v`): `kg_l2_aux` keeps the bitplanes for the MCU.

### 5.9 RPC (`08`)

Calls of the core to the firmware: a core with a CPU (the self-drawing boot menu of
karabas-go-core-menu, later other cores) asks for services of the MCU (files, settings, the core
list, the time). The MCU is the SPI master, so a call is a mailbox: the CPU writes a **request**
into a buffer of the core and raises IRQ bit 8, the MCU reads it, does it and writes the
**response** back. The MCU can also push **events** into a slot of the core. One call at a time;
payloads up to 512 bytes each way; a 16-bit sum (the bytes from the first header byte to the end of
the payload added) on every request, response and event. FPGA side: `kg_l2_rpc`
(`kg_mcu2` `ENABLE_RPC`, caps bit 10).

| C | Name | → | ← | Notes |
|---|---|---|---|---|
| `01` | `HELLO` | firmware RPC version, Z | core RPC version, max payload (2), services (2) | Sent once after `SYS STATUS` with caps bit 10. The core raises IRQ bit 8 only after it, so an older firmware never sees INT held high by a target it does not know |
| `02` | `STATUS` | | flags, request length (2) | Z first. flags: bit 0 a request waits for its response, bit 1 the event slot is free, bit 2 HELLO came |
| `03` | `REQ` | | id, service, function, length (2), payload, sum (2) | Z first. The read of the sum ends IRQ bit 8 for this request; the request stays (and can be read again after a bad sum) until its response |
| `04` | `RESP` | id, status, length (2), payload, sum (2), Z | 0 taken / 1 refused | Refused: a bad sum, another id, no request waiting, a length over 512: the MCU sends it again |
| `05` | `EVENT` | service, code, length (2), payload, sum (2), Z | 0 taken / 1 refused | Only while `STATUS` bit 1; refused when the slot is full (the CPU reads the event, then frees the slot) |

`SYS IRQ` answers a second byte of pending bits (bit 0 = target 08); an older firmware reads only
the first, an older core gives 0 for the second (reading past the data gives 0).

Response status: 0 ok, 1 unknown service / function, 2 bad arguments, 3 not found, 4 I/O error,
5 denied, 6 busy (call again). Strings in a payload end with a 0 byte; numbers are little-endian.

In the USB drive mode (the card is the PC's) the link is still served: SYS answers (but SD_INFO), CFG GET reads a copy of
karabas.ini kept before the PC got the card, the other CFG calls, CORES and HID are busy.

Services (`src/fpga/rpc_server.c`; the plan of the menu: karabas-go-core-menu `docs/RPC.md`):

| Service | F | Name | In | Out |
|---|---|---|---|---|
| 0 SYS | 0 | `INFO` | | firmware build (8), board, RPC version, services (2), USB drive mode (0 none, 1 SD, 2 SD2, 3 SD2 not found; an older firmware sends 12 bytes) |
| | 1 | `TIME_GET` | | year (2), month, day, hour, minute, second, weekday |
| | 2 | `TIME_SET` | as `TIME_GET` | |
| | 3 | `I2C_SCAN` | | 16 bytes: bit a % 8 of byte a / 8 = the device at address a answers (0x08..0x77; the board test core: RTC 68, EEPROM 50, LED matrix 70 / 71, the I/O extender of the Go 41) |
| | 4 | `LED` | mask, on | | the on-board LED n + 1 is held at bit n of on while bit n of mask is set (the board test core); mask 0 gives them back to the firmware, as does the start of the next core |
| | 5 | `SD_INFO` | | the SD card of the MCU (SD1, the board test core; read only): mounted (0 / 1), card type (0 none, 1 SDSC, 2 SDHC / SDXC), sectors (4), FatFs type (0 none, 1 FAT12, 2 FAT16, 3 FAT32, 4 exFAT), total KB (4), free KB (4: FFFFFFFF unknown), the volume label (UTF-8, "" none); type and sectors also when the mount failed. The free space is the count of FSInfo / FatFs (exFAT: its bitmap), never a scan of the FAT (seconds on a big card, APP held): without a count it is unknown. Busy (6) in the USB drive mode |
| 1 CFG | 0 | `GET` | file, (section, key)… | per pair: 1 and the value, or 0 and "" (not in the file); one read of the file |
| | 1 | `SET` | file, (section, key, value)… | | written into the file at once, one rewrite for all |
| | 2 | `SAVE` | file | | the firmware takes the settings again (`load_setup`, the LED matrix as saved) |
| | 3 | `PREVIEW` | file, mode (`none`, `clock`, `meter`), (section, key, value)… | | the LED matrix shows the `[matrix]` values given (not written) and the clock / the meters of mode; no values: back to `karabas.ini` |
| 2 CORES | 0 | `SCAN` | | count (2), in the order of `[setup] core_sort` |
| | 1 | `ITEM` | index (2) | file name, name, build, id, flags (bit 0 visible, bits 1.. the type) |
| | 2 | `LAUNCH` | file name | | the core is replaced: no response |
| | 3 | `AUTOLOAD` | | timeout (s, 0 none), file name |
| 3 HID | 0 | `PADS` | | per joystick port: source (0 none, 1 driver ini, 2 default map, 3 XInput), VID (2), PID (2), driver file |
| | 1 | `FORGET` | port | | the driver ini of its pad becomes `.bak` |
| | 2 | `LEARN_START` | | | the learning of a USB pad (`apps/pad_learn`) starts: the first pad whose report changes |
| | 3 | `LEARN_STATE` | | state, button, last button, seconds left, VID (2), PID (2), mapped (2), held (2), the rule of the last button, the saved file | state: 0 idle, 1 waiting for a pad, 2 / 3 settle / rest, 4 press, 5 release, 6 done, 7 saved, 8 failed; buttons Up Down Left Right A B C X Y Z Start Mode = bits 0..11 |
| | 4 | `LEARN_DO` | 0 stop, 1 skip, 2 back, 3 save | | |

CFG file `0` is `karabas.ini` and CORES, HID are for a menu core only: the boot core, or a core whose HELLO lists CORES. A boot core
with RPC draws the menu itself: the firmware shows no menu, no FT812, and leaves the autoload to it.

## 6. Core start

`configure()` in `src/fpga/core_loader.c`:

1. Bitstream through the configuration pins.
2. `SYS STATUS` until it answers `'K' 'G' 2` (100 ms at most; INT may already be high from the
   coldboot cause), then `SYS IRQ_SRC` (clears coldboot). No answer: the core is reported as not
   running.
3. `OSD COLORS` (white on `081028`, alpha 3) if the core has the 256 × 128 OSD.
4. `SYS INIT 1`.
5. `SYS HW_SETUP`: board, then DVI only.
6. `RTC SET` with all registers, `SYS SETVAL` per switch, `RTC SET` again; `IMG MOUNT` of the
   saved images.
7. `SYS HW_SETUP`: the two build ids.
8. ROMs: `LOAD START 0`, `DATA`…, `END 0`.
9. `FL` autoload (after the ROMs: a file over a ROM of the core file wins).
10. `HID KBD` with an empty report (through the `INPUT` task, only while the menu of the core is
    hidden).
11. `SYS INIT 0`.

There is no font download (the font is in the MCU) and no caps query apart from `STATUS`.

## 7. Choosing the link: core.json and kgo2

- `core.json`: `"link": 2` at the top level or per board (`boards.minig.link`), default 1.
- kgo2: the CORE record gets the field `link` (offset 16, 1 byte; the record grows, older readers
  skip it; the firmware only parses it, it decides by the section type). A v2 bitstream goes into a new section type **10 `BITSTREAM_LINK2`**, not into 9.
  An older firmware then finds no bitstream for its board and treats the file as no core for this
  board, instead of loading it and talking v1 to it. make_core writes type 10 for a board with
  link 2 and refuses `link` 2 for a `.kg1/.kg2/.kg3` output.
- Strings of the menu are UTF-8 (Cyrillic names); a v1 core keeps ASCII / CP866 as now.

## 8. OSD drawing in the firmware

- u8g2 (BSD 2-clause) in `third_party/u8g2`, a full-buffer display of 256 × 128 in MCU RAM (4 KB).
  The firmware sends only the runs of tiles that changed.
- A display layer under the menu code: the text grid backend of v1 (`lib/osd`, `osd_ui`) and the
  u8g2 backend of v2 share the menu, the file browser and the dialogs (toast, progress, busy,
  message / confirm, section 5.3): the callers (`hotkey_popup()`, the loaders, `bk_save`,
  `uknc_sav`) ask for a dialog, not for cells. The OLED (backlog) can reuse the u8g2 backend.
- The screens (`ui/ui2`, `ui/osd2`; agreed on the board 2026-11-12):
  - Font `u8g2_font_6x12_t_cyrillic` only (bold fonts were tried and dropped: letters merge),
    `u8g2_font_5x8_t_cyrillic` for hotkeys, notes, the clock, disabled rows.
  - Header (y 0..11, a line at 12): the core name from the left edge on the main page; on every
    level under it a triangle "back" and the title (the page path "Options > Video", the app,
    "Disk 8: /dir" of a browser). At the right the clock, in a browser its extensions instead.
  - 9 rows of 12 pixels, no hint lines; a scrollbar at the right.
  - The setup apps (Setup RTC, Map USB gamepad) and About are
    the last items of the main page, after a separator.
  - Switches: Left / Right and the hotkey step the options; Enter opens a screen of the options
    (radio buttons, Up / Down, Enter takes one, Esc back). Off / On, No / Yes, Disabled / Enabled
    pairs are a check box; radio groups are circles.
  - Focus: a filled bar / button has the focus, a frame marks the chosen one while the focus is
    elsewhere (the port of Map USB gamepad).
  - Boxes in the middle with a clear margin, the toast in the middle (zoomed popup), About with
    the logo of the OLED (no "mini" on a Go). The background opaque (COLORS alpha 3).
  - Activity icons: drawn by the core (`kg_l2_osd_overlay` `ICONS_N` ..., see the migration guide),
    not by the firmware. Later (backlog): marquee of long names.
## 9. FPGA side (karabas-go-modules)

- `kg_l2_link`: the transport: the byte shifter on `MCU_SCK` (`kg_spi_slave`, 8 bits, mode 0,
  the chip-select deglitch of `kg_mcu_link`), the packet position, the byte bus of the targets
  (`stb`, `tgt`, `cmd`, `n`, `d`) and their answers (`rq`, `rq_m` -> `q`, one turnaround byte),
  `INT` = the or of their pending bits.
- Targets `kg_l2_sys`, `_hid`, `_load`, `_osd`, `_img`, `_port`, `_rtc`, `_aux`, each behind an
  `ENABLE_*` parameter.
- `kg_mcu2`: the module of a v2 core. **Nothing of link v1 in it** (no frames, no bursts, no
  reply queue) and **its own core-side ports** (section 11): a core moves to v2 by an edit of its
  top (the new ports), the INT pin (the IO1 line, already the burst ready of burst cores) and
  `"link": 2`.
- Clock: `sim/mcu/run_kg_mcu2_sim.sh` of the modules runs the link with a 28 MHz core clock at
  16 and 32 MHz SCK, 12.5 MHz at 32 MHz SCK, and fast cores at a slow SCK (65 MHz at 16 and 8 MHz,
  125 MHz at 16 MHz). Before modules #41 a core clock above about 4 x SCK
  answered reads one byte early (bk at 65 MHz with 16 MHz SCK).
- Saving per core: the reply queue (512 × 24) and `kg_burst_rx` go, the OSD font RAM goes; the OSD
  bitmap needs 4 KB (2 RAMB16).

## 10. History (done)

One branch and one PR per phase and repository; all phases are done (firmware #129-#136, merged
into master by #145, released 26111315).

| Phase | Work |
|---|---|
| Ф0 | This specification |
| Ф1 | Firmware refactor without a change of behaviour: the services (input, OSD, img, loaders, RTC, ports, matrix) call a link interface instead of building frames, and the popups go through the dialog API (section 5.3) on the text backend; v1 byte for byte, checked on the board |
| Ф2 | Transport + SYS / HID / LOAD: `kg_l2_link`, `kg_l2_sys` / `_hid` / `_load`, `kg_mcu2` with a testbench and an MCU model; firmware `link2` (blocking SPI transfers in `APP`, INT read in the main loop; no DMA, no task of its own); make_core `link` + section 10. Pilot core on Mini G: **vic20** (done: Mini G hw ok); the boot core moves in Ф3 / Ф5 (it needs the OSD, the ESP and MIDI ports). Moving a core: karabas-go-modules `docs/link_v2_migration.md` |
| Ф3 | OSD: `kg_l2_osd` (bitmap, `POPUP` windows with zoom), u8g2 and the display layer, the Cyrillic fonts, the menu and the dialogs in graphics |
| Ф4 | IMG: mounter, `.sav`, write protection, physical floppy drives |
| Ф5 | PORT, RTC, AUX, `SPI_CONTROL` / FT812 |
| Ф6 | The other cores one by one (each its own PR), the ports of MiSTle cores first (atarist, macplus, atari2600) |

The firmware keeps link v1 for every core that is not moved.

## 11. The schemes of v1 in v2

A v2 core does not keep the v1 way of a feature: each one is redone as packets that carry a whole
message (a report, a register set, a sector) in one chip select, and as core-side ports of
`kg_mcu2` that give the core the whole message with one strobe. The cores are edited for it.

| Scheme | v1 | v2 packets | `kg_mcu2` core side | Edit of a core |
|---|---|---|---|---|
| Core start | `CMD_CAPS` polled with NOPs, `INIT_START`, `HW_SETUP` one frame per byte (18 frames for the ids), font download | `SYS STATUS` (magic, caps, OSD size), `INIT 1`, `HW_SETUP` board, DVI, then the 16 build characters in one packet, `INIT 0`; no font | `HW_ID`, `DVI_ONLY`, `CORE_BUILD`, `FW_BUILD`, `BUSY` | none (the same signals) |
| Firmware polling | a NOP frame every main loop pass, replies in the bytes of any frame | `INT` (IO1, active high), `SYS IRQ` / per-target events read only when INT is high | `MCU_INT` | the IO1 pin: the burst ready of the burst cores, an output of `kg_mcu2` |
| OSD switches | `CMD_SWITCHES` one frame per switch, `kg_soft_switches` keeps a 16-bit command | `SYS SETVAL` (index, value) × n (the firmware sends one pair per packet) | `SW_WR`, `SW_INDEX`, `SW_VALUE` | the switch registers of the core take `SW_WR` (a helper that keeps them as a vector replaces `kg_soft_switches`) |
| Buttons | `CMD_BTN`, `CMD_MATRIX_BTN` | `SYS BTN` 0..1 board, 2..5 matrix | `BTNS` | none / the matrix buttons if used |
| Reload of the core | `CMD_FLASHBOOT` in the reply queue | `SYS IRQ_SRC` bit 1, `SYS RELOAD` | `RELOAD_REQ`, `RELOAD_CORE` | none |
| FT812, SD2 | `CMD_SPI_CONTROL`, chip selects passed through | `SYS SPI_CONTROL` | `FT_*`, `SD2_*` as in `kg_mcu` | none |
| Keyboard | 7 frames per report, `KB_DAT0..5` / `KB_STATUS` change byte by byte | `HID KBD` 8 bytes | `KBD` (56 bits) with `KBD_WR` | the keyboard of the core takes the report on `KBD_WR` (no half-new report) |
| PS/2, XT | one frame per byte, `KB_SCANCODE_UPD` / `XT_SCANCODE_UPD` toggle | `HID PS2` (kind, code) × n (the firmware sends one byte per packet) | `PS2_CODE`, `PS2_KIND`, `PS2_WR` strobe | toggle detectors -> the strobe |
| Keyboard commands of the core | `CMD_PS2_SCANCODE` in the reply queue | `HID EVENT` (count, (cmd, arg) × n), IRQ bit 1 | `PS2_CMD`, `PS2_CMD_WR` | none |
| Mouse | 4 frames, `MS_UPD` toggles on the buttons byte, `MS_Z` 4 bits | `HID MOUSE` 4 bytes | `MS_X`, `MS_Y`, `MS_Z` (8 bits), `MS_B`, `MS_WR` | toggle -> strobe; the wheel is 8 bits |
| Joysticks | 2 frames per port, 13 bits | `HID JOY` (port, lo, hi) × n | `JOY_L`, `JOY_R` (16 bits), `JOY_WR` | the width (bits 13..15 free for more buttons) |
| ROMs | `ROMLOADER`, `ROMBANK` + `ROMDATA` per byte or bursts (second chip select, `BURST_READY`) | `LOAD START 0`, `DATA` (kind, address, up to 512 bytes), `END 0` | the `LD_*` bus, `LD_KIND` 0 | `ROMLOAD_*` -> `LD_*` |
| File loader (`fileloader` cores) | `FILELOADER` 3 / 2 / 0 levels, `FILEBANK` + `FILEDATA` or bursts | `LOAD START 1` / `DATA` / `END 1` | `LD_*`, `LD_KIND` 1, `LD_START` = the reset of a new file | `FILELOAD_*` -> `LD_*` |
| FL items (ioctl) | `IOCTL_STATE`, `SLOT`, `SIZE` × 8 twice, `EXT`, `BANK` + `DATA` or bursts | `LOAD START 2` (slot, size, extension in one packet) / `DATA` / `END 2` | `LD_*`, `LD_KIND` 2, `LD_SLOT`, `LD_SIZE` | as above; `FILE_TO_ROM` / `FILE_WR_EACH` choices go into the core |
| Flow control of loads | `BURST_READY` line | `LOAD STATUS` before a packet (caps bit 4) | `LD_READY` | a core with a slow memory drives it |
| File mounter: mount | `IMG_SLOT`, `IMG_SIZE` × 8 twice, `IMG_FLAGS`, `IMG_PHYS` | `IMG MOUNT` (slot, size, flags: write protected, physical drive) in one packet | the mount signals of `img_sdc` | none for the drives (`sdc_*` stays) |
| File mounter: requests | `IMG_LBA` × 4 + `IMG_SEC` in the reply queue, NOP pulls, a lost frame = an incomplete request | `IMG EVENT` (kind, slot, LBA) on IRQ bit 3 | `img_sdc` sector interface | none |
| File mounter: read | 512 × (`BUF_DATA`) + banks or a burst, then the sum in `IMG_LBA` 0/1, then `IMG_SEC` | `IMG RDATA` (slot, 512 bytes, sum) one packet, the core answers taken / bad sum in the same packet; `IMG DONE` | as now | none |
| File mounter: write | the request, then 512 data frames + the sum queued behind it (the queue had to hold them), pulled with up to 4000 NOPs | `IMG WDATA` (slot) one packet reads the 512 bytes and the sum from the sector buffer of the core | as now | none |
| Backup RAM (`.sav`) | `CMD_BK_MOUNT` frames, `CMD_IMG_FLUSH` both ways | `IMG EVENT` kind 4 (size, kind), `IMG FLUSH`, its answer `IMG EVENT` kind 3 | `kg_bk_ram` on the new img side | none |
| DSKCHG (Go floppy) | `CMD_FDD_CHNG` 49 d 00 / 49 d v | `IMG EVENT` kind 5, `IMG FDD_CHNG` | as now | none |
| RTC | `CMD_RTC` one frame per register: all at the start, the time every 500 ms; a write of the CPU one frame per register | `RTC SET` (first register, N values): the whole set at the start, the time registers (one packet of about 10 bytes) every 500 ms; `RTC GET` (count, (register, value) × n) on IRQ bit 6 | the register RAM of `kg_mcu_rtc` (`RTC_A`, `RTC_DI`, `RTC_DO`, `RTC_CS`, `RTC_WR_N`) in `kg_l2_rtc` | none |
| USB serial (ZiFi, RS232) | `CMD_UART` one frame per byte both ways, `idx` counter, DLL / DLM frames | `PORT 0`: `GET` many bytes per packet, `PUT` (one byte per packet now), `STATUS` (free, available, the divisor) | rx / tx fifos with valid / ready, `UART_DLL` / `UART_DLM` | byte + counter -> fifo handshake |
| ESP8266 | `CMD_ESP_UART` per byte, the byte held until ack | `PORT 1` as port 0 | fifos | as above |
| MIDI IN | `CMD_MIDI` per byte with a counter, `MIDI_LOST` | `PORT 2 PUT` | `MIDI_DATA`, `MIDI_WR` | `MIDI_LOST` goes |
| Printer | `PRINTER_DATA` per byte + status request, `PRINTER_STATUS` | `PORT 3 GET` many bytes, `PSTAT` the status | a tx fifo, `PRN_BUSY` | none |
| LED matrix, meters, debug | `CMD_MATRIX_CTL` / `PIXEL` / `BITPLAN`, `AUDIO_PEAKS_L/R`, `DEBUG_*` frames in the queue | `AUX MATRIX` (control + 32 bytes), `AUX PEAKS`, `AUX DEBUG`, IRQ bit 7 | a bitplane buffer the core draws into (pixels or rows), the peaks, the debug words | `MATRIX_CMD` -> the buffer |
| OSD | 32 × 26 cells with 16 colours, font in the FPGA, popup mode, popup numbers for the core's own overlay | `OSD WRITE` tiles of a 256 × 128 bitmap, `POPUP` windows with zoom, `COLORS` | `kg_l2_osd` over the video (replaces `kg_osd_overlay`) | the video path takes the new overlay |
| Bursts | a second chip select, header, `BURST_READY`, caps bits 0..2 | none: every packet is a burst | — | the burst pins go |
| Integrity | the chip select filter, sums on sectors, the core asks again | the chip select filter, sums on sectors (read and write), a request repeated after 200 ms | — | — |

What stays in the firmware as now: the formats of the RTC (`rtc_mode`), the hotkeys, the menu
from the core file, the files and the images; what stays in the core as now: the drives behind
the sector interface, the CPU reset during a load.
