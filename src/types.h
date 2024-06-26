#pragma once

#include "config.h"

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
	char name[16+1];
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
	state_file_loader
};
