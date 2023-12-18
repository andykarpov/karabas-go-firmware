#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
#include <SPI.h>

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

#define PIN_EXT_BTN1 0
#define PIN_EXT_BTN2 1
#define PIN_EXT_LED1 2
#define PIN_EXT_LED2 3

#define CMD_USB_KBD 0x01
#define CMD_USB_MOUSE 0x02
#define CMD_JOYSTICK 0x03
#define CMD_BTN 0x04
#define CMD_SWITCHES 0x05

#define CMD_USB_GAMEPAD 0x11
#define CMD_USB_JOYSTICK 0x12

#define CORE_TYPE_BOOT 0x0
#define CORE_TYPE_OSD 0x1
#define CORE_TYPE_OTHER 0x2

#define CORE_OSD_TYPE_SWITCH 0x0
#define CORE_OSD_TYPE_NSWITCH 0x1
#define CORE_OSD_TYPE_TRIGGER 0x2
#define CORE_OSD_TYPE_HIDDEN 0x3

#define MAX_CORES 255
#define MAX_CORES_PER_PAGE 16

#define FILE_POS_CORE_ID 4
#define FILE_POS_CORE_NAME 36
#define FILE_POS_CORE_BUILD 68
#define FILE_POS_CORE_VISIBLE 76
#define FILE_POS_CORE_ORDER 77
#define FILE_POS_CORE_TYPE 78
#define FILE_POS_CORE_EEPROM_BANK 79
#define FILE_POS_BITSTREAM_LEN 80
#define FILE_POS_ROM_LEN 84
#define FILE_POS_EEPROM_DATA 256
#define FILE_POS_SWITCHES_DATA 512
#define FILE_POS_BITSTREAM_START 1024
#define FILENAME_BOOT "boot.kg1"

#define APP_COREBROWSER_MENU_OFFSET 3

typedef struct {
	uint8_t cmd;
	uint8_t addr;
    uint8_t data;
} queue_spi_t;

typedef struct {
	char name[32];
	char filename[32];
	uint8_t order;
	bool visible;
	uint8_t type;
} core_list_item_t;

typedef struct {
	char name[16];
} core_osd_option_t;

typedef struct {
	uint8_t type;
	uint8_t bits;
	uint8_t def;
	char name[16];
	char hotkey[16];
	uint8_t keys[3];
	core_osd_option_t options[8];
	uint8_t options_len;
} core_osd_t;

typedef struct {
	char id[32];
	char build[8];
	char name[32];
	char filename[32];
	uint8_t order;
	bool visible;
	uint8_t type;
	uint32_t bitstream_length;
	uint8_t eeprom_bank;
	core_osd_t osd[32];
	uint8_t osd_len;
} core_item_t;

void spi_queue(uint8_t cmd, uint8_t addr, uint8_t data);
void spi_send(uint8_t cmd, uint8_t addr, uint8_t data);
core_list_item_t get_core_list_item();
void read_core_list();
void read_core(const char* filename);
uint32_t fpga_configure(const char* filename);
void halt(const char* msg);
void process_in_cmd(uint8_t cmd, uint8_t addr, uint8_t data);
void on_time();
void on_keyboard();
void core_browser(uint8_t vpos);
void send_rom_byte(uint32_t addr, uint8_t data);

uint32_t file_read32(uint32_t pos);
uint32_t file_read24(uint32_t pos);
uint16_t file_read16(uint32_t pos);

#endif
