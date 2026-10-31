# MCU ↔ FPGA protocol

[Русская версия](protocol.ru.md) · Burst transfers: [burst_protocol.md](burst_protocol.md) · Core files and `core.json`: [karabas-go-tools/make_core](https://github.com/andykarpov/karabas-go-tools/tree/master/make_core)

The RP2040 (MCU) and the FPGA core talk over one SPI link. Over it the MCU:

- sends input events, OSD drawing, ROMs and files, RTC registers and settings;
- receives what the core wants from it: RTC writes, UART bytes, LED matrix drawing, debug values.

This document describes the link as the firmware implements it (`src/fpga/link.cpp`, the `CMD_*` codes in `src/config.h`). On the FPGA side every core has its own copy of `mcu.vhd`. TS-Conf (`karabas-go-core-tsconf/src/rtl/mcu/mcu.vhd`) is the reference implementation.

## 1. Physical layer

| Signal | Direction | Notes |
|---|---|---|
| `MCU_SCK` | MCU → FPGA | SPI clock. 16 MHz by default; a core can set another clock with `spi_freq` in its `core.json`. |
| `MCU_MOSI` | MCU → FPGA | |
| `MCU_MISO` | FPGA → MCU | Driven by the FPGA only while `MCU_SS` is low. |
| `MCU_SS` (`MCU_CS_N`) | MCU → FPGA | Frame select, active low. |
| `MCU_IO` / `MCU_SPI_IO` | both | Extra lines, depending on the board. They carry the FT812 and SD2 chip selects (routed by the FPGA, see `CMD_SPI_CONTROL`) and the burst chip select / READY ([burst_protocol.md](burst_protocol.md)). |

SPI mode 0, MSB first, 8-bit bytes.

The same SPI0 bus also reaches the FT812 video chip. When a core enables it (`CMD_SPI_CONTROL`), the FPGA routes the bus to the FT812 using its own chip select.

## 2. Frames

Every transfer is one **frame** of 3 bytes with `MCU_SS` low around it:

```
MCU_SS   ‾‾\______________________/‾‾
MOSI        CMD     ADDR    DATA          MCU → FPGA
MISO        CMD'    ADDR'   DATA'         FPGA → MCU, in the same transfer
```

- `CMD` selects the command; `ADDR` and `DATA` are its two arguments.
- In the same transfer the FPGA shifts out its own frame `CMD' ADDR' DATA'` for the MCU, usually the next entry of its output queue. When it has nothing to say it sends `FF 00 00` (`CMD_NOP`).
- The FPGA can talk only when the MCU starts a transfer. The firmware therefore sends a `CMD_NOP` frame whenever the link was idle in a main loop pass (`link_poll()`).
- Every reply frame whose `CMD` has a handler is dispatched to it (`link_on()` in the firmware). Replies are ignored while a core is being configured.

**Multi-byte values** are sent as several frames with the same `CMD`: `ADDR` is the byte index, starting at 0, least significant byte first (`spi_send16/24/32/64`). Example, `CMD_IOCTL_SIZE` = 1000 (0x3E8):

```
51 00 E8   51 01 03   51 02 00   ...   51 07 00
```

**Bank + data pairs.** ROM, file loader and ioctl data use bank frames for address bits 31..8 and data frames for bits 7..0:

```
BANK  addr=0 data=A15..8 / addr=1 data=A23..16 / addr=2 data=A31..24   (when they change)
DATA  addr=A7..0 data=byte
```

Bursts replace these pairs with a much faster transfer, see section 7.

### FPGA side

The cores use `spi_slave.vhd` with a 24-bit word (`N => 24`):

- `do_valid` strobes when a frame has been received. It can last 2 clocks, so take it on its rising edge.
- `di_req` asks for the next outgoing word. The cores keep a small FIFO (`queue`) of outgoing frames and pop it on `di_req`; some cores keep a few NOP frames queued.

Commands that the core does not know must be ignored.

## 3. Core start sequence

`do_configure()` in `src/fpga/core_loader.cpp`, for every core load:

1. The bitstream is sent through the FPGA configuration pins (not this link).
2. `CMD_CAPS` query, answered only by cores that support bursts or backup RAM. It is repeated until the core answers (up to 100 ms), so nothing else goes to a core whose link is still in reset after CONF_DONE. See section 7.
3. `CMD_INIT_START` — the core shows itself busy.
4. `CMD_HW_SETUP` `(0, HW_ID)` and `(1, dvi_only)`.
5. OSD font: `CMD_OSD` font reset and the font data.
6. ROMs: `CMD_ROMLOADER` on, the ROM data, `CMD_ROMLOADER` off.
7. OSD switch values (`CMD_SWITCHES`) and the RTC registers (`CMD_RTC`).
8. Keyboard cleared: `CMD_USB_KBD` 0..6 = 0.
9. `CMD_INIT_DONE` — the core may start.

## 4. Commands MCU → FPGA

| Code | Name | ADDR | DATA | Notes |
|---|---|---|---|---|
| `01` | `CMD_USB_KBD` | 0 = modifiers, 1–6 = key codes | USB HID boot report byte | Not sent while the OSD is open. |
| `02` | `CMD_USB_MOUSE` | 0 = X, 1 = Y, 2 = wheel, 3 = buttons | signed delta / button bits | One frame set per USB report that moves or changes the buttons; the core adds the deltas on the buttons frame. Not sent while the OSD is open. |
| `03` | `CMD_JOYSTICK` | 0/1 = left low/high byte, 2/3 = right low/high byte | 16-bit joystick state | Bits: 0 connected, 1 up, 2 down, 3 left, 4 right, 5 start, 6 A, 7 B, 8 C, 9 X, 10 Y, 11 Z, 12 mode. USB gamepads are mapped onto the same bits (`drivers/*.ini`). |
| `04` | `CMD_BTN` | 0 / 1 = on-board button | 1 pressed, 0 released | Only for a button whose press action is `core` (the core file header, make_core `buttons`; the default). |
| `05` | `CMD_SWITCHES` | OSD item index | value | Switches: the selected option. Triggers: 1, then 0 after 100 ms. The index is the position in the flat item list of the core file, on whatever page of the menu the item is. |
| `06` | `CMD_ROMBANK` | 0 / 1 / 2 | ROM address bits 15..8 / 23..16 / 31..24 | |
| `07` | `CMD_ROMDATA` | address bits 7..0 | ROM byte | |
| `08` | `CMD_ROMLOADER` | 0 | 1 = start, 0 = end | The core usually holds its CPU in reset while active. |
| `09` | `CMD_SPI_CONTROL` | 0 | bit 0 FT812 SPI on, bit 1 FT812 video on, bit 2 SD2, bit 3 ESP8266, bit 4 FT812 reset | Control register for the peripherals behind the FPGA. |
| `0B` | `CMD_PS2_SCANCODE` | 0 = PS/2 set 2 byte, 1 = XT (set 1) byte, bit 7 = release | scancode byte | From the USB or PS/2 keyboard. |
| `0C` | `CMD_FILEBANK` | 0 / 1 / 2 | file address bits 15..8 / 23..16 / 31..24 | File loader (`fileloader` cores). |
| `0D` | `CMD_FILEDATA` | address bits 7..0 | file byte | |
| `0E` | `CMD_FILELOADER` | 0 | bit 0 reset, bit 1 active | `3` then `2` before the file, `0` after it. |
| `20` | `CMD_OSD` | register | value | OSD overlay, see section 6. |
| `40` | `CMD_IMG_SLOT` | 0 | slot | File mounter (`F` OSD items), see section 6. |
| `41` | `CMD_IMG_SIZE` | byte index 0..7 | image size (LE) | First 0 (eject), then the real size. The core takes the image at byte 7. |
| `42` | `CMD_IMG_LBA` | 0 / 1 | sum bits 7..0 / 15..8 | 16-bit sum of the sector read, after its data. |
| `43` | `CMD_IMG_SEC` | slot | 1 = done, 2 = error | End of a sector request. |
| `44` | `CMD_IMG_BUF_BANK` | 0 | sector offset bits 8..15 | Sector data of a read request (classic frames). |
| `45` | `CMD_IMG_BUF_DATA` | offset bits 7..0 | byte | Same; also the burst type of the sector data, address = offset. |
| `46` | `CMD_IMG_PHYS` | slot | 1 = drive DS0, 2 = DS1 | The slot is a physical floppy drive (Karabas Go), after its size 0 frames. |
| `47` | `CMD_IMG_FLUSH` | slot | 0 | Backup RAM: write the dirty sectors of the slot now (cores with caps bit 3), see section 6. |
| `50` | `CMD_IOCTL_SLOT` | 0 | slot | Ioctl file loader (`FL` OSD items), see section 6. |
| `51` | `CMD_IOCTL_SIZE` | byte index 0..7 | file size (LE) | |
| `52` | `CMD_IOCTL_BANK` | byte index 0..2 | file address bits 15..8 / 23..16 / 31..24 | |
| `53` | `CMD_IOCTL_DATA` | address bits 7..0 | file byte | |
| `54` | `CMD_IOCTL_EXT` | character index | extension character | Lowercase, without the dot. |
| `55` | `CMD_IOCTL_STATE` | 0 | 1 = start, 0 = end | |
| `63` | `CMD_MATRIX_BTN` | button 0..3 | 1 / 0 | LED matrix add-on buttons ([matrix.md](matrix.md)). |
| `F7` | `CMD_CAPS` | 0 = query | protocol version (1) | Capability query, section 7. |
| `F8` | `CMD_ESP_UART` | 0 | byte | Byte to the ESP8266 module behind the FPGA. |
| `F9` | `CMD_HW_SETUP` | 0 = board, 1 = DVI only | 0: `HW_ID` (1 Go, 2 Mini, 3 Mini G); 1: 0 / 1 | |
| `FA` | `CMD_RTC` | register | value | RTC register; the register set depends on `rtc_mode` (MC146818A, DS1307, ZX Evolution, MSM6242B). |
| `FC` | `CMD_UART` | running index | byte | Byte from the USB serial port; `ADDR` changes with every byte, so the core can see a new one. |
| `FD` | `CMD_INIT_START` | 0 | 0 | Core configuration starts (BUSY = 1). |
| `FE` | `CMD_INIT_DONE` | 0 | 0 | Core configuration done (BUSY = 0). |
| `FF` | `CMD_NOP` | 0 | 0 | Poll: lets the FPGA send its next frame. |

`11` (`CMD_USB_GAMEPAD`) and `12` (`CMD_USB_JOYSTICK`) are deprecated and no longer sent: USB pads reach the core as `CMD_JOYSTICK`.

## 5. Commands FPGA → MCU

These are sent in the reply bytes of any transfer.

| Code | Name | ADDR | DATA | Firmware action |
|---|---|---|---|---|
| `0B` | `CMD_PS2_SCANCODE` | PS/2 command | argument | Keyboard commands from the core: `ED` = LEDs, `F0` = scancode set. |
| `30` | `CMD_DEBUG_ADDRESS` | high byte | low byte | 16-bit value printed to the debug log. |
| `31` | `CMD_DEBUG_DATA` | high byte | low byte | Same. |
| `42` | `CMD_IMG_LBA` | byte index 0..3 | sector number (LE) | File mounter sector request, before `CMD_IMG_SEC`. |
| `43` | `CMD_IMG_SEC` | slot | 1 = read, 2 = write | The request. The firmware serves it from its main loop, see section 6. |
| `44` / `45` | `CMD_IMG_BUF_BANK` / `_DATA` | 0 / offset bits 7..0 | offset bits 8..15 / byte | Sector data of a write request, after the request. |
| `44` | `CMD_IMG_BUF_BANK` | `10` / `11` | sum bits 7..0 / 15..8 | 16-bit sum of the sector written, after its data. |
| `47` | `CMD_IMG_FLUSH` | slot | 1 = done, 2 = a write failed | Answer to `47 slot 00`: no dirty sectors left. |
| `48` | `CMD_BK_MOUNT` | 0..3 / `10` / `80` | size in sectors (LE) / img slot / kind | Backup RAM: the core asks for its save file; `80` last (kind 0 = `.sav`). Section 6. |
| `60` | `CMD_MATRIX_CTL` | 0 | bit 0 clear, bit 1 update, bit 2 set brightness (bits 7..4), bit 3 audio meter mode | LED matrix ([matrix.md](matrix.md)). |
| `61` | `CMD_MATRIX_PIXEL` | Y (bits 7..4), X (bits 3..0) | color: 0 black, 1 red, 2 yellow, 3 green | |
| `62` | `CMD_MATRIX_BITPLAN` | byte 0..31 | bitmap byte | a row of one color: 0..7 the left matrix red rows 0..7, 8..15 its green, 16..23 / 24..31 the right matrix; bit n is column n of the matrix |
| `70` / `71` | `CMD_AUDIO_PEAKS_L` / `_R` | high byte | low byte | 16-bit audio peak for the matrix meters. |
| `F7` | `CMD_CAPS` | caps | max_mhz | Answer to the capability query, section 7. |
| `F8` | `CMD_ESP_UART` | 0 | byte | Byte from the ESP8266 module. |
| `FA` | `CMD_RTC` | register | value | The core's CPU wrote an RTC register; the firmware updates the RTC chip. |
| `FB` | `CMD_FLASHBOOT` | 0 | core id | Reload a core. Now it always reloads the current core (ZX Next hard reset). |
| `FC` | `CMD_UART` | 0 = byte at 115200 (ZiFi), 1 = DLL, 2 = DLM, 3 = byte at the DLL/DLM speed (EVO RS232) | byte / divisor | Bytes to the USB serial port. |
| `FF` | `CMD_NOP` | | | Nothing to say. |

## 6. Sequences

### ROMs (`roms` in core.json)

```
08 00 01                        ROM loader on
06 00 a15..8  06 01 a23..16  06 02 a31..24      only when bits 31..8 change
07 a7..0 byte                   one frame per byte
...
08 00 00                        ROM loader off
```

The core writes each byte to `address + offset`. Cores detect a write from a new address, or from the `CMD_ROMDATA` strobe.

### File loader (`fileloader` cores)

```
0E 00 03   0E 00 02             reset + active, then active
0C ... / 0D ...                 bank + data, as for ROMs, file offset from 0
0E 00 00                        done
```

### Ioctl file loader (`FL` OSD items)

```
55 00 01                        start
50 00 slot
51 i size_i  (i = 0..7)         size 0, then the real size
54 i char_i                     extension, e.g. "rk"
52 i bank_i  (i = 0..2) / 53 a7..0 byte     bank + data
55 00 00                        end
```

The file is sent when it is chosen in the OSD. An `FL` item with `autoload` (bit 7 of its slot byte) and a file that exists (its `dir` / `filename` from `core.json`, or the last chosen one) is also sent at core start, after the switches and before `CMD_INIT_DONE`, e.g. the Kickstart of the NanoMig core.

### File mounter (`F` OSD items)

The image file of a slot stays open (read-write if possible) while the core runs. Sectors are 512 bytes, numbered from the start of the file.

Mount: at core start for every `F` item with a saved file, and when a file is selected in the OSD.

Eject: Del / Backspace on the `F` item in the OSD, or `[ Eject ]` at the top of its file browser. The firmware sends only the size 0 frames and forgets the file in the core settings. Browsing other directories does not change the mounted image.

```
40 00 slot
41 i 00      (i = 0..7)         size 0: eject
41 i size_i  (i = 0..7)         the image; not sent without a file
```

Physical floppy drive (Karabas Go: `board_has_floppy()`, and an `F` item with the `physical` flag of `core.json`, bit 0 of the byte after the item type). Its file browser has the entries `[ Physical DF0 ]` / `[ Physical DF1 ]` (drive DS0 / DS1 of the floppy connector) after `[ Eject ]`. A drive is in one slot only: choosing it ejects it from another slot. The core settings keep the name `:phys0` / `:phys1` as the file of the slot. Each mount is a disk change for the core; the next size frames of the slot (eject, an image) end it.

```
40 00 slot
41 i 00      (i = 0..7)         size 0
46 slot drive                   1 = DS0, 2 = DS1
```

Read request (the core sends its frames in the reply bytes):

```
FPGA: 42 i lba_i (i = 0..3), 43 slot 01
MCU:  sector data: classic frames 44 00 offset_hi / 45 offset_lo byte
      (or a burst of type 45, address = offset 0..511)
MCU:  42 00 sum_lo, 42 01 sum_hi
MCU:  43 slot 01 (done) or 43 slot 02 (error: no file, past its end, or an incomplete request)
```

Write request:

```
FPGA: 42 i lba_i (i = 0..3), 43 slot 02,
      then 512 bytes: 44 00 offset_hi / 45 offset_lo byte, then 44 10 sum_lo, 44 11 sum_hi
MCU:  polls with NOPs until the 512 bytes and the sum are in, writes the sector
MCU:  43 slot 01 (done) or 43 slot 02 (error: data lost, no file)
```

One request at a time: the core waits for `43 slot status` before the next one.

A frame used to get lost on the link now and then (on Mini G: chip select spikes and a reply queue that took two words in one frame, both fixed in the core). Requests are still checked end to end:

- the firmware takes a request only with all four `42` bytes since `42 00`, and answers `43 slot 02` otherwise;
- the core takes a read only with 512 bytes and the same sum; the firmware writes a sector only with all 512 bytes and the same sum;
- the core sends a request again after an error answer, or after no answer in 200 ms (a lost `43`).

The firmware sends the sector data of a read as one burst when the core has burst caps, as classic frames otherwise (and for whatever a burst did not send). The core checks the sum either way and passes a sector on only when it is complete and right. The data of a write request does not fit the reply queue of `mcu.vhd` (512 entries) together with the request, so the core queues it while the queue has room. A short last sector of a read is padded with zeros.

### Backup RAM (`.sav`, cores with caps bit 3)

The battery RAM of a cartridge (NES and the like) is kept in a file next to the games. The core decides its size (only it knows the rules of the mapper) and asks for the file after a game is loaded; the firmware does not parse the game. The file is a raw dump of the backup RAM, as MiSTer and the emulators keep it.

```
FPGA: 48 00 size_0, 48 01 size_1, 48 02 size_2, 48 03 size_3   size in 512-byte sectors (LE)
FPGA: 48 10 slot                                                the img slot for the file
FPGA: 48 80 kind                                                0 = .sav (n = save state n, later)
MCU:  opens or creates <save dir>/<game name without its extension>.sav with at least size sectors
      (new bytes 0), then mounts it in the slot as the file mounter does: 40 00 slot, 41 i 00,
      41 i file_size_i. Without a file (no game name, no sd card, read only): the size 0 frames only.
```

The save dir is `save_dir` of the core header (`FILE_POS_SAVE_DIR`, 32 bytes), or `<fileloader dir>/saves` when it is empty. The core reads its sectors with ordinary read requests before its CPU starts, and writes the sectors its CPU changed with ordinary write requests, a second after the last write (or a few seconds after the first one, for games that write all the time). A core that gets no mount in about a second goes on without a save (a firmware without this command).

Flush, before the firmware loads another game, another core, reboots (the hotkey, `AT+KARABAS=boot`) or enters USB mass storage:

```
MCU:  47 slot 00
FPGA: write requests of the dirty sectors (the firmware serves them while it waits)
FPGA: 47 slot 01 (no dirty sectors left) or 47 slot 02 (a write failed)
MCU:  waits up to 3 s for the answer, then syncs and closes the file (size 0 frames to the core)
```

### OSD overlay (`CMD_OSD`)

| ADDR | Register |
|---|---|
| `01` | Show: 1 / 0 |
| `02` | Popup: bit 0 = show, bits 7..4 = popup number (its meaning is up to the core's overlay), 0 = hide |
| `0F` | Clear |
| `10` / `11` | Cursor X / Y |
| `12` / `13` | Character / attribute at the cursor |
| `14` | No operation |
| `20` | Font reset |
| `21` / `22` | Font byte / font byte write strobe |

The firmware keeps a copy of the screen and sends only the cells that changed.

### RTC

At core start the firmware sends all RTC registers; then every 500 ms it sends the time registers. Both use `CMD_RTC (register, value)`. When the core's CPU writes a register, the core sends `CMD_RTC` back and the firmware writes the RTC chip. Formats:

- **MC146818A:** BCD or binary, selected by the DM bit (2) of register B: 0 BCD, 1 binary; always 24 h. Registers A and B come from the EEPROM bank of the core; an erased one (FFh) reads as A 26h, B 02h (BCD). Register B is sent with the mode the time is in, SET cleared.
- **DS1307:** BCD.
- **ZX Evolution:** MC146818A registers, always BCD.
- **MSM6242B** (`rtc_mode` 3, the Amiga clock at $DC0000): registers 0-12, one BCD digit each: S1 S10 MI1 MI10 H1 H10 D1 D10 MO1 MO10 Y1 Y10 W (year mod 100, W = 0 for Sunday), 24 h. The control registers D, E, F are in the core. The core sends the digits the CPU writes; the firmware sets the clock chip 100 ms after the last of them (the CPU writes the time a digit at a time) and does not send the time in between.

### USB printer: data and status only

| Direction | Frame (hex) | Meaning |
|---|---|---|
| FPGA -> MCU | `13 00 xx` | One raw printer byte |
| FPGA -> MCU | `14 00 00` | Optional status request; enables updates |
| MCU -> FPGA | `14 00 ss` | Printer status only |

`ss` is `80` (BUSY bit 7 set) or `00`; bits 6..0 are zero. There are no
per-byte acknowledgements or SPI reset commands. Nonzero addresses are ignored.
BUSY means disconnected, unknown/not-ready USB status, service error/reset,
low queue capacity (32 or fewer free bytes), or a deferred DATA byte. The queue
remains 2048 bytes; the reserve absorbs SPI traffic already in transit. FPGA
initializes BUSY=1. BUSY clears automatically when capacity/readiness returns.

DATA is enqueued immediately in the APP handler. Consecutive bytes do not need
acknowledgement. MCU publishes changed status from its APP loop, and answers
explicit status requests. A status frame never confirms receipt of a byte.
The core enables updates with a status request or its first DATA frame; old cores
receive no unsolicited printer traffic. A known status is required before BUSY=0,
including after connection with paper absent. USB status is polled once per second.

FPGA must account for SPI FIFO and status propagation latency when limiting Z80
writes: cached BUSY=0 is not a reservation of queue space. One byte arriving at a
full queue is retained in a separate protocol latch and enqueued when space returns,
before subsequent bytes. Any further overrun is counted as rejected; it does not
latch BUSY permanently. A session change discards the deferred byte. The 32-byte
reserve is not a guarantee for arbitrarily deep FPGA FIFOs. The transport has no
end-to-end delivery guarantee if FPGA overruns the queue or loses SPI frames.

Core loading clears link state and requests service reset if queued data or a
service fault remains. Explicit recovery is available through the local
`printer_reset()` API, not an SPI command. Session-checked enqueue prevents data
from crossing a disconnect/reset race. Already delivered bytes cannot be retracted.

### Printer diagnostic snapshot (`15`)

Historical debugging extension: removed from the GO FPGA after the SPI fix
passed repeated hardware printing tests. Current GO HDL ignores `15` and does
not contain the printer counters or CRC logic. The format below documents the
older diagnostic builds; RP2040 diagnostic support remains available for those.

MCU requests `15 00 tag`. Matching GO HDL replies with `15 80 tag`, then
28 frames `15 index byte` (index `00..1B`), then `15 81 tag`.
The first 24 bytes are six little-endian uint32 values: Z80 OUT count, Z80 data
CRC, rejected OUT count, accepted DATA FIFO count, completed SPI DATA count,
SPI DATA CRC. The last four bytes are the CRC of the preceding 24 bytes.
All CRCs use CRC-32/ISO-HDLC (initial/final XOR FFFFFFFF, reflected EDB88320).
The snapshot freezes at request reception. Replies have lowest queue priority;
a request while a snapshot is being enqueued is ignored. Missing, reordered or
corrupt replies are discarded; a later diagnostic request may retry with a new
tag. Printer DATA is never retried by this command. Commands `13`/`14` are unchanged.
See [hardware diagnostics](usb_printer.md#fpga--spi-loss-localisation).

## 7. Capabilities and burst transfers

`CMD_CAPS` (`F7 00 01`) asks the core what it supports. The firmware then polls with up to 16 NOP frames, and sends the query and the polls again until the core answers or 100 ms have passed. A core that supports bursts or backup RAM answers `F7 caps max_mhz` in a reply frame:

- caps bit 0: burst write;
- caps bit 1: the core drives `BURST_READY`;
- caps bit 2: burst read (planned);
- caps bit 3: backup RAM (`CMD_BK_MOUNT`, `CMD_IMG_FLUSH`, section 6): the firmware flushes the save of the core before it loads another game or core.

Old cores do not answer and stay on classic frames (their load takes the 100 ms longer).

Bursts use a second chip select. One 5-byte header (type + 32-bit address) is followed by up to 512 data bytes. They replace the bank + data frames of ROMs (`07`), the file loader (`0D`) and ioctl (`53`). The full specification is in [burst_protocol.md](burst_protocol.md).

With `debug=1` the log shows the caps answer and the transfer times.
