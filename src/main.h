#pragma once

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

#define PIN_FT_RESET 0     // TP9 - /FT_PD
#define PIN_MCU_SD2_CS 7  // TP11 - FPGA TP2 (M9)
#define PIN_MCU_FT_CS 15  // TP13 - FPGA TP3 (M10)

#define PIN_MCU_SPI_IO0 14 // TP12 - FPGA TP4 (M11)
#define PIN_MCU_SPI_IO1 2  // TP10 - FPGA TP1 (N9)

#define PIN_EXT_BTN1 0
#define PIN_EXT_BTN2 1
#define PIN_EXT_LED1 2
#define PIN_EXT_LED2 3

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

#define CORE_OSD_TYPE_SWITCH 0x00
#define CORE_OSD_TYPE_NSWITCH 0x01
#define CORE_OSD_TYPE_TRIGGER 0x02
#define CORE_OSD_TYPE_HIDDEN 0x03
#define CORE_OSD_TYPE_TEXT 0x04

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
#define FILENAME_BOOT "boot.kg1"
#define FILENAME_FBOOT "/boot.kg1"

#define APP_COREBROWSER_MENU_OFFSET 5

#ifndef DEBUG
#define DEBUG 0
#endif

#ifndef FT_OSD 
#define FT_OSD 0
#endif

#if DEBUG
#define d_begin(...) Serial.begin(__VA_ARGS__);
#define d_print(...)    Serial.print(__VA_ARGS__)
#define d_printf(...)    Serial.printf(__VA_ARGS__)
#define d_write(...)    Serial.write(__VA_ARGS__)
#define d_println(...)  Serial.println(__VA_ARGS__)
#define d_flush(...)  Serial.flush(__VA_ARGS__)
#else
#define d_begin(...)
#define d_print(...)
#define d_printf(...)
#define d_write(...)
#define d_println(...)
#define d_flush(...)
#endif

typedef struct {
	uint8_t cmd;
	uint8_t addr;
    uint8_t data;
} queue_spi_t;

typedef struct {
	bool flash;
	char name[32+1];
	char filename[32+1];
	char build[8+1];
	uint8_t order;
	bool visible;
	uint8_t type;
} core_list_item_t;

typedef struct {
	char name[16+1];
} core_osd_option_t;

typedef struct {
	uint8_t type;
	uint8_t def;
	char name[32+1];
	char hotkey[16+1];
	uint8_t keys[2];
	core_osd_option_t options[MAX_OSD_ITEM_OPTIONS];
	uint8_t options_len;
	uint8_t val;
	uint8_t prev_val;
} core_osd_t;

typedef struct {
	uint8_t val;
	uint8_t prev_val;
} core_eeprom_t;

typedef struct {
	char id[32+1];
	char build[8+1];
	char name[32+1];
	char filename[32+1];
	bool flash;
	uint8_t order;
	bool visible;
	uint8_t type;
	uint32_t bitstream_length;
	uint8_t eeprom_bank;
	uint8_t rtc_type;
	core_osd_t osd[MAX_OSD_ITEMS];
	uint8_t osd_len;
	core_eeprom_t eeprom[MAX_EEPROM_ITEMS];
	bool osd_need_save;
	bool eeprom_need_save;
	char dir[32+1];
	char last_file[32+1];
	char file_extensions[32+1];
} core_item_t;

typedef struct {
	char name[32+1];
} file_list_item_t;

enum osd_state_e {
    state_main = 0,
    state_rtc,
	state_core_browser,
	state_file_loader,
    state_about,
    state_info,
};

enum osd_rtc_state_e {
    state_rtc_hour = 0,
    state_rtc_minute,
    state_rtc_second,
    state_rtc_day,
    state_rtc_month,
    state_rtc_year,
    state_rtc_dow
};

void spi_queue(uint8_t cmd, uint8_t addr, uint8_t data);
void spi_send(uint8_t cmd, uint8_t addr, uint8_t data);
core_list_item_t get_core_list_item(bool is_flash);
void read_core_list();
void read_file_list(bool forceIndex);
void read_core(const char* filename);
void send_font();
uint32_t fpga_configure(const char* filename);
void do_configure(const char* filename);
void halt(const char* msg);
void process_in_cmd(uint8_t cmd, uint8_t addr, uint8_t data);
void on_time();
void on_keyboard();
void core_browser(uint8_t vpos);
void menu(uint8_t vpos);
bool is_flashfs(const char* filename);

void core_osd_save(uint8_t pos);
void core_file_loader_save();
void core_osd_send(uint8_t pos);
void core_osd_trigger(uint8_t pos);
void core_osd_send_all();

uint8_t core_eeprom_get(uint8_t pos);
void core_eeprom_set(uint8_t pos, uint8_t val);
void core_eeprom_send(uint8_t pos);
void core_eeprom_send_all();

void read_roms(const char* filename);
void send_rom_byte(uint32_t addr, uint8_t data);

void send_file_byte(uint32_t addr, uint8_t data);

void file_seek(uint32_t pos, bool is_flash);
uint8_t file_read(bool is_flash);
size_t file_read_bytes(char *buf, size_t len, bool is_flash);
void file_get_name(char *buf, size_t len, bool is_flash);
int file_read_buf(char *buf, size_t len, bool is_flash);
uint32_t file_read32(uint32_t pos, bool is_flash);
uint32_t file_read24(uint32_t pos, bool is_flash);
uint16_t file_read16(uint32_t pos, bool is_flash);

void osd_handle(bool force);

void osd_print_header();
void osd_print_logo(uint8_t x, uint8_t y);
void osd_print_line(uint8_t y);
void osd_print_space();
void osd_print_footer();
void osd_clear();

void osd_init_overlay();
void osd_init_popup(uint8_t event_type);
void osd_init_rtc_overlay();
void osd_init_about_overlay();
void osd_init_info_overlay();
void osd_init_core_browser_overlay();
void osd_init_file_loader_overlay(bool initSD, bool recreateIndex);

void popupFooter();

uint8_t core_eeprom_get(uint8_t pos);
void core_eeprom_set(uint8_t pos, uint8_t val);

#endif
