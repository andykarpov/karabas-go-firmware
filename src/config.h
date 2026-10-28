#pragma once

#define HW_ID_GO 1
#define HW_ID_MINI 2
#define HW_ID_MINIG 3

// HW_ID is set per board env in platformio.ini (pio run -e go|mini|minig)
#ifndef HW_ID
#error "HW_ID is not defined, build with pio run -e go|mini|minig"
#endif

#ifndef ENABLE_MSC
#define ENABLE_MSC 1
#endif

#ifndef BUILD_VER
#define BUILD_VER DEVBUILD
#endif

#define STRINGIZE(x) #x
#define STRINGIZE_VALUE_OF(x) STRINGIZE(x)

#if HW_ID==HW_ID_GO

#define HOST_PIN_DP   25

#define PIN_MCU_SPI_SCK 22 // sck
#define PIN_MCU_SPI_CS 21  // ss
#define PIN_MCU_SPI_RX 20 // miso
#define PIN_MCU_SPI_TX 23 // mosi

#define PIN_SD_SPI_SCK 9 
#define PIN_SD_SPI_CS 8 
#define PIN_SD_SPI_RX 11 
#define PIN_SD_SPI_TX 10 
#define SD_CS_PIN PIN_SD_SPI_CS

#define PIN_I2C_SDA 12 
#define PIN_I2C_SCL 13 

#define PIN_JOY_SCK 5 
#define PIN_JOY_DATA 6 
#define PIN_JOY_LOAD 4 
#define PIN_JOY_P7 1 

#define PIN_CONF_INIT_B 3 
#define PIN_CONF_PRG_B 16 
#define PIN_CONF_IO1 18 
#define PIN_CONF_CLK 17 
#define PIN_CONF_DONE 19 

#define PIN_FT_RESET 0     // TP9 - /FT_PD
#define PIN_MCU_SD2_CS 7  // TP11 - FPGA TP2 (M9)
#define PIN_MCU_FT_CS 15  // TP13 - FPGA TP3 (M10)

#define PIN_MCU_SPI_IO0 14 // TP12 - FPGA TP4 (M11)
#define PIN_MCU_SPI_IO1 2  // TP10 - FPGA TP1 (N9)

#define PIN_EXT_BTN1 0
#define PIN_EXT_BTN2 1
#define PIN_EXT_LED1 2
#define PIN_EXT_LED2 3

#elif HW_ID==HW_ID_MINI

#define HOST_PIN_DP   24

#define PIN_MCU_SPI_SCK 22 // sck
#define PIN_MCU_SPI_CS 21  // ss
#define PIN_MCU_SPI_RX 20 // miso
#define PIN_MCU_SPI_TX 23 // mosi

#define PIN_SD_SPI_SCK 2 
#define PIN_SD_SPI_CS 1 
#define PIN_SD_SPI_RX 0 
#define PIN_SD_SPI_TX 3 
#define SD_CS_PIN PIN_SD_SPI_CS

#define PIN_I2C_SDA 12 
#define PIN_I2C_SCL 13 

#define PIN_CONF_INIT_B 11 
#define PIN_CONF_PRG_B 19 
#define PIN_CONF_IO1 10 
#define PIN_CONF_CLK 9 
#define PIN_CONF_DONE 8 

#define PIN_MCU_SD2_CS 16 // io2 
#define PIN_MCU_FT_CS 17  // io3

#define PIN_MCU_SPI_IO0 14 // io0
#define PIN_MCU_SPI_IO1 15 // io1
#define PIN_MCU_SPI_IO4 18 // io4 (will be gone for 50MHz clock)

#define PIN_BTN1 4
#define PIN_BTN2 6
#define PIN_LED1 7
#define PIN_LED2 5

#elif HW_ID==HW_ID_MINIG

#define HOST_PIN_DP   24

#define PIN_MCU_SPI_SCK 22 // sck
#define PIN_MCU_SPI_CS 21  // ss
#define PIN_MCU_SPI_RX 20 // miso
#define PIN_MCU_SPI_TX 23 // mosi

#define PIN_SD_SPI_SCK 2 
#define PIN_SD_SPI_CS 1 
#define PIN_SD_SPI_RX 0 
#define PIN_SD_SPI_TX 3 
#define SD_CS_PIN PIN_SD_SPI_CS

#define PIN_I2C_SDA 12 
#define PIN_I2C_SCL 13 

#define PIN_CONF_INIT_B 11 
#define PIN_CONF_PRG_B 19 
#define PIN_CONF_IO1 10 
#define PIN_CONF_CLK 9 
#define PIN_CONF_DONE 8 

#define PIN_MCU_SD2_CS 16 // io2 
#define PIN_MCU_FT_CS 17  // io3

#define PIN_MCU_SPI_IO0 14 // io0
#define PIN_MCU_SPI_IO1 15 // io1
#define PIN_MCU_SPI_IO4 18 // io4 (will be gone for 50MHz clock)
#define PIN_MCU_SPI_IO5 26 // io5

#define PIN_FT_RESET 27
#define PIN_MIDI_RESET 28
#define PIN_ESP_RESET 29

#define PIN_BTN1 4
#define PIN_BTN2 6
#define PIN_LED1 7
#define PIN_LED2 5

#endif // HW_ID

// burst transfers (docs/burst_protocol.md). Same RP2040 pins on all boards; on Go the FPGA
// port index is swapped (IO0 = MCU_SPI_IO[1], IO1 = MCU_SPI_IO[0]).
// MCU_IO4 (GPIO18 on Mini / Mini G) must stay an input: it carries the FT812 clock after the
// revF clock mod and is disconnected on Rev.G.
#define PIN_BURST_CS PIN_MCU_SPI_IO0
#define PIN_BURST_READY PIN_MCU_SPI_IO1
#define BURST_MAX_LEN 512

#define CMD_USB_KBD 0x01
#define CMD_USB_MOUSE 0x02
#define CMD_JOYSTICK 0x03
#define CMD_BTN 0x04
#define CMD_SWITCHES 0x05
#define CMD_ROMBANK 0x06
#define CMD_ROMDATA 0x07
#define CMD_ROMLOADER 0x08
#define CMD_SPI_CONTROL 0x09
#define CMD_PS2_SCANCODE 0x0B
#define CMD_FILEBANK 0x0C
#define CMD_FILEDATA 0x0D
#define CMD_FILELOADER 0x0E

#define CMD_USB_GAMEPAD 0x11 // deprecated, not sent
#define CMD_USB_JOYSTICK 0x12 // deprecated, not sent
#define CMD_PRINTER_DATA 0x13 // FPGA -> MCU: addr=0, data=raw byte
#define CMD_PRINTER_STATUS 0x14 // addr=0, status bit7=BUSY; no acknowledgement/reset command
#define CMD_PRINTER_DIAG 0x15 // tagged read-only FPGA printer counters/CRC snapshot

#define CMD_OSD 0x20

#define CMD_DEBUG_ADDRESS 0x30
#define CMD_DEBUG_DATA 0x31

// file mounter (F OSD items), see docs/protocol.md section 6 and storage/img_mount.cpp:
// mount: SLOT, SIZE 0 (eject), SIZE (the image, 8 bytes LE)
// sector request from the fpga: LBA (4 bytes LE), SEC (addr = slot, data = 1 read / 2 write);
// sector data: BUF_BANK + BUF_DATA (or a burst of type BUF_DATA); result to the fpga: SEC
// (addr = slot, data = 1 done / 2 error)

#define CMD_IMG_SLOT 0x40
#define CMD_IMG_SIZE 0x41
#define CMD_IMG_LBA 0x42
#define CMD_IMG_SEC 0x43
#define CMD_IMG_BUF_BANK 0x44
#define CMD_IMG_BUF_DATA 0x45
// a physical floppy drive for an F slot (Karabas Go), after its size 0 frames: addr = slot,
// data = 1 drive DS0, 2 drive DS1. The core takes it back at the next size frames of the slot.
#define CMD_IMG_PHYS 0x46
// backup RAM (docs/protocol.md, "Backup RAM"), cores with CAPS_BK: the fpga asks for its save file
// with 48 00..03 size in sectors (LE), 48 10 img slot, 48 80 kind (0 .sav, n save state n);
// the mcu mounts it in the slot (CMD_IMG_SLOT / CMD_IMG_SIZE, size 0 without a file)
#define CMD_BK_MOUNT 0x48
// mcu -> fpga 47 slot 00: write the dirty sectors of the slot now; fpga -> mcu 47 slot 01 done,
// 47 slot 02 a write failed
#define CMD_IMG_FLUSH 0x47

// fileloader files will be transferred in the following order:
// 1. send SLOT num  - 1 byte
// 2. send file SIZE - 4 bytes
// 3. send file EXT  - 3 bytes lowercase
// 4. while not EOF: 
// 4.1. send bank when pos % 256 = 0
// 4.2. send data bytes (0-255)
// on the FPGA side the transfer begins on sending a SLOT number
// and ends on the last byte was received
// the core itself should decide what to do with the incoming data, e.g. parse header, write data directly to the memory location, etc

#define CMD_IOCTL_SLOT 0x50 // (0, uint8_t): set current slot number (0-3)
#define CMD_IOCTL_SIZE 0x51 // (0-7, uint8_t): fileloader file size
#define CMD_IOCTL_BANK 0x52 // (0-7, uint8_t): fileloader bank
#define CMD_IOCTL_DATA 0x53 // (0-255, uint8_t): fileloader byte
#define CMD_IOCTL_EXT 0x54 // (0-3, uint8_t): fileloader file ext
#define CMD_IOCTL_STATE 0x55 // 1 - active, 0 - inactive

// LED matrix registers
#define CMD_MATRIX_CTL 0x60 // adr 0, bit 0 - clear, bit1 - update, bit2 - brightness, bit 4-7 - brightness level
#define CMD_MATRIX_PIXEL 0x61 // adr: xy, data - color (00 - black, 01 - red, 10 - yellow, 11 - green: the HT16K33 order, as the cores use it)
#define CMD_MATRIX_BITPLAN 0x62 // 0...31 - address of byte, 32 bytes total (16 bytes per matrix)
#define CMD_MATRIX_BTN 0x63 // bits 0-3 represents keypresses for shield buttons

// Audio data (slow peaks)
#define CMD_AUDIO_PEAKS_L 0x70
#define CMD_AUDIO_PEAKS_R 0x71

#define CMD_CAPS 0xF7 // core capability query / answer, see docs/burst_protocol.md
#define CMD_ESP_UART 0xF8
#define CMD_HW_SETUP 0xF9
#define CMD_RTC 0xFA
#define CMD_FLASHBOOT 0xFB
#define CMD_UART 0xFC
#define CMD_INIT_START 0xFD
#define CMD_INIT_DONE 0xFE
#define CMD_NOP 0xFF

#define CORE_TYPE_BOOT 0x00
#define CORE_TYPE_OSD 0x01
#define CORE_TYPE_FILELOADER 0x02
#define CORE_TYPE_HIDDEN 0xff

#define CORE_OSD_TYPE_SWITCH 0x00        // drop-down like control to select a value from predifined options 
#define CORE_OSD_TYPE_NSWITCH 0x01       // non-volatile dropdown. the selected value is not stored on change
#define CORE_OSD_TYPE_TRIGGER 0x02       // sends a pulse while pressed. the value also is not stored anywhere
#define CORE_OSD_TYPE_HIDDEN 0x03        // hidden value 
#define CORE_OSD_TYPE_TEXT 0x04          // just a text line (32 characters wide)
#define CORE_OSD_TYPE_FILEMOUNTER 0x05   // mounts a selected file image as virtual drive (img_*)
#define CORE_OSD_TYPE_FILELOADER 0x06    // immediately transfer a selected file to the fpga side (ioctl_*)

// osd item flags (the byte after the type in the core file)
#define CORE_OSD_FLAG_PHYS 0x01          // F item: can be a physical floppy drive (board_has_floppy)

#define MAX_CORES 255 // cores_len is uint8_t
#define MAX_FILES 255
#define MAX_FILE_SLOTS 8
#define MAX_JOY_DRIVERS 255
#define MAX_CORES_PER_PAGE 16
#define MAX_OSD_ITEMS 32
#define MAX_OSD_ITEM_OPTIONS 8
#define MAX_EEPROM_ITEMS 256
#define MAX_EEPROM_BANKS 4
#define NO_EEPROM_BANK 255

#define FILE_POS_CORE_ID 4
#define FILE_POS_CORE_NAME 36
#define FILE_POS_CORE_BUILD 68
#define FILE_POS_CORE_VISIBLE 76
#define FILE_POS_CORE_ORDER 77
#define FILE_POS_CORE_TYPE 78
#define FILE_POS_CORE_EEPROM_BANK 79
#define FILE_POS_BITSTREAM_LEN 80
#define FILE_POS_ROM_LEN 84
#define FILE_POS_RTC_TYPE 88
#define FILE_POS_FILELOADER_DIR 89
#define FILE_POS_FILELOADER_FILE 121
#define FILE_POS_FILELOADER_EXTENSIONS 153
#define FILE_POS_SPI_FREQ 185
#define FILE_POS_SAVE_DIR 186 // 32 bytes: the dir of the backup RAM files ("" = <fileloader dir>/saves)
#define FILE_POS_BUTTONS 218 // 3 bytes per button (btn1..btn6): press action, hold action, hold time / 100 ms; FF = not set
#define FILE_POS_EEPROM_DATA 256
#define FILE_POS_SWITCHES_DATA 512
#define FILE_POS_MATRIX 768 // 256 bytes: the LED matrix settings (make_core "matrix", lib/common kg_matrix.h); 0 = core
#define FILE_POS_BITSTREAM_START 1024

#if HW_ID==HW_ID_GO
#define FILENAME_BOOT "boot.kg1"
#define FILENAME_FBOOT "/boot.kg1"
#define CORE_EXT ".kg1"
#elif HW_ID==HW_ID_MINI
#define FILENAME_BOOT "boot.kg2"
#define FILENAME_FBOOT "/boot.kg2"
#define CORE_EXT ".kg2"
#elif HW_ID==HW_ID_MINIG
#define FILENAME_BOOT "boot.kg3"
#define FILENAME_FBOOT "/boot.kg3"
#define CORE_EXT ".kg3"
#endif

#define APP_COREBROWSER_MENU_OFFSET 5

#define SORT_HASH_LEN 4
#define SORT_FILES_MAX 8000

#ifndef WAIT_SERIAL
#define WAIT_SERIAL 0
#endif


// debug output goes through src/diag/debug_out (one CDC writer, see there)
#include "diag/debug_out.h"

#ifdef __cplusplus
extern "C" {
#endif
// d_print: a string; d_println: an optional string and the line end; numbers through d_printf
#define d_print(s)     do { if (hw_setup.debug_enabled) debug_print(s); } while (0)
#define d_printf(...)  do { if (hw_setup.debug_enabled) debug_printf(__VA_ARGS__); } while (0)
#define d_println(...) do { if (hw_setup.debug_enabled) debug_println(__VA_OPT__(__VA_ARGS__,) NULL); } while (0)
#define d_flush()      do { if (hw_setup.debug_enabled) debug_flush(); } while (0)

#ifdef __cplusplus
}
#endif
