#pragma once

#define HW_ID_GO 1
#define HW_ID_MINI 2

#ifndef HW_ID
#define HW_ID 2
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

#define PIN_MCU_SD2_CS 16  
#define PIN_MCU_FT_CS 17 

#define PIN_MCU_SPI_IO0 14 
#define PIN_MCU_SPI_IO1 15
#define PIN_MCU_SPI_IO4 18

#define PIN_BTN1 4
#define PIN_BTN2 6
#define PIN_LED1 7
#define PIN_LED2 5

#endif // HW_ID

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

#define CMD_USB_GAMEPAD 0x11
#define CMD_USB_JOYSTICK 0x12

#define CMD_OSD 0x20

#define CMD_DEBUG_ADDRESS 0x30
#define CMD_DEBUG_DATA 0x31

#define CMD_IMG_SLOT 0x40
#define CMD_IMG_SIZE 0x41
#define CMD_IMG_LBA 0x42
#define CMD_IMG_SEC 0x43
#define CMD_IMG_BUF_BANK 0x44
#define CMD_IMG_BUF_DATA 0x45

// todo: other IMG commands here

// fileloader files will be transferred in the following order:
// 1. send SLOT num  - 1 byte
// 2. send file SIZE - 8 bytes
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

#define MAX_CORES 255
#define MAX_FILES 255
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
#define FILE_POS_EEPROM_DATA 256
#define FILE_POS_SWITCHES_DATA 512
#define FILE_POS_BITSTREAM_START 1024

#if HW_ID==HW_ID_GO
#define FILENAME_BOOT "boot.kg1"
#define FILENAME_FBOOT "/boot.kg1"
#define CORE_EXT ".kg1"
#elif HW_ID==HW_ID_MINI
#define FILENAME_BOOT "boot.kg2"
#define FILENAME_FBOOT "/boot.kg2"
#define CORE_EXT ".kg2"
#endif

#define APP_COREBROWSER_MENU_OFFSET 5

#ifndef WAIT_SERIAL
#define WAIT_SERIAL 0
#endif

#define d_begin(...)   if (hw_setup.debug_enabled) Serial.begin(__VA_ARGS__);
#define d_print(...)   if (hw_setup.debug_enabled) Serial.print(__VA_ARGS__);
#define d_printf(...)  if (hw_setup.debug_enabled) Serial.printf(__VA_ARGS__);
#define d_write(...)   if (hw_setup.debug_enabled) Serial.write(__VA_ARGS__);
#define d_println(...) if (hw_setup.debug_enabled) Serial.println(__VA_ARGS__);
#define d_flush(...)   if (hw_setup.debug_enabled)  Serial.flush(__VA_ARGS__);
