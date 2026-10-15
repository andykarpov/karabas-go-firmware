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
2. `CMD_INIT_START` — the core shows itself busy.
3. `CMD_HW_SETUP` `(0, HW_ID)` and `(1, dvi_only)`.
4. `CMD_CAPS` query, answered only by cores that support bursts. See section 7.
5. OSD font: `CMD_OSD` font reset and the font data.
6. ROMs: `CMD_ROMLOADER` on, the ROM data, `CMD_ROMLOADER` off.
7. OSD switch values (`CMD_SWITCHES`) and the RTC registers (`CMD_RTC`).
8. Keyboard cleared: `CMD_USB_KBD` 0..6 = 0.
9. `CMD_INIT_DONE` — the core may start.

## 4. Commands MCU → FPGA

| Code | Name | ADDR | DATA | Notes |
|---|---|---|---|---|
| `01` | `CMD_USB_KBD` | 0 = modifiers, 1–6 = key codes | USB HID boot report byte | Not sent while the OSD is open. |
| `02` | `CMD_USB_MOUSE` | 0 = X, 1 = Y, 2 = wheel, 3 = buttons | signed delta / button bits | |
| `03` | `CMD_JOYSTICK` | 0/1 = left low/high byte, 2/3 = right low/high byte | 16-bit joystick state | Bits: 0 connected, 1 up, 2 down, 3 left, 4 right, 5 start, 6 A, 7 B, 8 C, 9 X, 10 Y, 11 Z, 12 mode. USB gamepads are mapped onto the same bits (`drivers/*.ini`). |
| `04` | `CMD_BTN` | 0 / 1 = on-board button | 1 pressed, 0 released | |
| `05` | `CMD_SWITCHES` | OSD item index | value | Switches: the selected option. Triggers: 1, then 0 after 100 ms. |
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
| `50` | `CMD_IOCTL_SLOT` | 0 | slot | Ioctl file loader (`FL` OSD items), see section 6. |
| `51` | `CMD_IOCTL_SIZE` | byte index 0..7 | file size (LE) | |
| `52` | `CMD_IOCTL_BANK` | byte index 0..2 | file address bits 15..8 / 23..16 / 31..24 | |
| `53` | `CMD_IOCTL_DATA` | address bits 7..0 | file byte | |
| `54` | `CMD_IOCTL_EXT` | character index | extension character | Lowercase, without the dot. |
| `55` | `CMD_IOCTL_STATE` | 0 | 1 = start, 0 = end | |
| `63` | `CMD_MATRIX_BTN` | button 0..3 | 1 / 0 | LED matrix add-on buttons ([MATRIX.md](../MATRIX.md)). |
| `F7` | `CMD_CAPS` | 0 = query | protocol version (1) | Capability query, section 7. |
| `F8` | `CMD_ESP_UART` | 0 | byte | Byte to the ESP8266 module behind the FPGA. |
| `F9` | `CMD_HW_SETUP` | 0 = board, 1 = DVI only | 0: `HW_ID` (1 Go, 2 Mini, 3 Mini G); 1: 0 / 1 | |
| `FA` | `CMD_RTC` | register | value | RTC register; the register set depends on `rtc_mode` (MC146818A, DS1307, ZX Evolution). |
| `FC` | `CMD_UART` | running index | byte | Byte from the USB serial port; `ADDR` changes with every byte, so the core can see a new one. |
| `FD` | `CMD_INIT_START` | 0 | 0 | Core configuration starts (BUSY = 1). |
| `FE` | `CMD_INIT_DONE` | 0 | 0 | Core configuration done (BUSY = 0). |
| `FF` | `CMD_NOP` | 0 | 0 | Poll: lets the FPGA send its next frame. |

`11` (`CMD_USB_GAMEPAD`) and `12` (`CMD_USB_JOYSTICK`) are deprecated.

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
| `60` | `CMD_MATRIX_CTL` | 0 | bit 0 clear, bit 1 update, bit 2 set brightness (bits 7..4), bit 3 audio meter mode | LED matrix ([MATRIX.md](../MATRIX.md)). |
| `61` | `CMD_MATRIX_PIXEL` | Y (bits 7..4), X (bits 3..0) | color: 0 black, 1 red, 2 green, 3 yellow | |
| `62` | `CMD_MATRIX_BITPLAN` | byte 0..31 | bitmap byte | |
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

### File mounter (`F` OSD items)

The image file of a slot stays open (read-write if possible) while the core runs. Sectors are 512 bytes, numbered from the start of the file.

Mount: at core start for every `F` item with a saved file, and when a file is selected in the OSD.

```
40 00 slot
41 i 00      (i = 0..7)         size 0: eject
41 i size_i  (i = 0..7)         the image; not sent without a file
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

A frame can get lost on the link now and then (seen on Mini G, cause not found), so requests are checked end to end:

- the firmware takes a request only with all four `42` bytes since `42 00`, and answers `43 slot 02` otherwise;
- the core takes a read only with 512 bytes and the same sum; the firmware writes a sector only with all 512 bytes and the same sum;
- the core sends a request again after an error answer, or after no answer in 200 ms (a lost `43`).

The firmware sends the sector data as classic frames: with bursts the NanoMig core on Mini G stopped taking its reply frames. The data of a write request does not fit the reply queue of `mcu.vhd` (512 entries) together with the request, so the core queues it while the queue has room. A short last sector of a read is padded with zeros.

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

- **MC146818A:** BCD or binary, selected by register B.
- **DS1307:** BCD.
- **ZX Evolution:** MC146818A registers, always BCD.

## 7. Capabilities and burst transfers

`CMD_CAPS` (`F7 00 01`) asks the core what it supports. The firmware then polls with up to 16 NOP frames. A core that supports bursts answers `F7 caps max_mhz` in a reply frame:

- caps bit 0: burst write;
- caps bit 1: the core drives `BURST_READY`;
- caps bit 2: burst read (planned).

Old cores do not answer and stay on classic frames.

Bursts use a second chip select. One 5-byte header (type + 32-bit address) is followed by up to 512 data bytes. They replace the bank + data frames of ROMs (`07`), the file loader (`0D`) and ioctl (`53`). The full specification is in [burst_protocol.md](burst_protocol.md).

`burst=0` in `karabas.ini` turns bursts off. With `debug=1` the log shows the caps answer and the transfer times.
