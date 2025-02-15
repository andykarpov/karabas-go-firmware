#pragma once

#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "types.h"
#include "FT812.h"
#include "OSD.h"
#include "RTC.h"
#include "SdFat.h"
#include "LittleFS.h"
#include "hid_app.h"
#include "ElapsedTimer.h"
#include "file.h"

#define SD2_CONFIG SdSpiConfig(PIN_MCU_SD2_CS, SHARED_SPI, SD_SCK_MHZ(16)) // SD2 SPI Settings

extern FT812 ft;
extern RTC zxrtc;
extern OSD zxosd;

extern bool has_fs;
extern bool has_sd;
extern bool has_ft;
extern bool need_redraw;

extern SdFat32 sd1;
extern File32 root1, file1, file2;
extern fs::Dir froot;
extern fs::File ffile, ffile2;

extern hid_keyboard_report_t usb_keyboard_report;
extern hid_mouse_report_t usb_mouse_report;

extern uint16_t joyL;
extern uint16_t joyR;
extern uint16_t joyUSB[MAX_USB_JOYSTICKS];
extern uint8_t joyUSB_len;

extern uint8_t osd_state;
extern core_item_t core;
extern core_file_slot_t file_slots[MAX_FILE_SLOTS];

extern bool is_osd_hiding;
extern ElapsedTimer hide_timer; 

extern hid_joy_config_t joy_drivers[MAX_JOY_DRIVERS];
extern uint8_t joy_drivers_len;

extern setup_t hw_setup;

extern file_list_sort_item_t files[SORT_FILES_MAX];
extern uint16_t files_len;
extern uint16_t file_sel;
extern uint16_t file_page_size;
extern uint16_t file_pages;
extern uint16_t file_page;
extern file_list_item_t cached_names[MAX_CORES_PER_PAGE];
extern uint16_t cached_file_from, cached_file_to;

void spi_queue(uint8_t cmd, uint8_t addr, uint8_t data);
void spi_send(uint8_t cmd, uint8_t addr, uint8_t data);
void spi_send16(uint8_t cmd, uint16_t data);
void spi_send24(uint8_t cmd, uint32_t data);
void spi_send32(uint8_t cmd, uint32_t data);
void spi_send64(uint8_t cmd, uint64_t data);
void process_in_cmd(uint8_t cmd, uint8_t addr, uint8_t data);

void do_configure(const char* filename);
uint32_t fpga_send(const char* filename);
void halt(const char* msg);

void osd_handle(bool force);

void read_core(const char* filename);
void read_roms(const char* filename);
void core_trigger(uint8_t pos);
void core_send(uint8_t pos);

void on_time();
void on_keyboard();

bool btn_read(uint8_t num);
void led_write(uint8_t num, bool on);

void load_setup();

int32_t msc_read_cb_sd (uint32_t lba, void* buffer, uint32_t bufsize);
int32_t msc_write_cb_sd (uint32_t lba, uint8_t* buffer, uint32_t bufsize);
void msc_flush_cb_sd (void);
bool msc_start_stop_cb_sd(uint8_t power_condition, bool start, bool load_eject);
