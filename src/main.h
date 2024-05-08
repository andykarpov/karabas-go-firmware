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

extern FT812 ft;
extern RTC zxrtc;
extern OSD zxosd;

extern bool has_fs;
extern bool has_sd;
extern bool has_ft;
extern bool need_redraw;

extern SdFat sd1;
extern FsFile root1, file1, fileIndex1;
extern fs::Dir froot;
extern fs::File ffile;

extern hid_keyboard_report_t usb_keyboard_report;
extern hid_mouse_report_t usb_mouse_report;

extern uint16_t joyL;
extern uint16_t joyR;
extern uint16_t joyUSB[4];
extern uint8_t joyUSB_len;

extern uint8_t osd_state;
extern core_item_t core;
extern core_file_slot_t file_slots[4];

extern bool is_osd_hiding;
extern ElapsedTimer hide_timer; 

extern hid_joy_config_t joy_drivers[255];
extern uint8_t joy_drivers_len;

void spi_queue(uint8_t cmd, uint8_t addr, uint8_t data);
void spi_send(uint8_t cmd, uint8_t addr, uint8_t data);
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

int32_t msc_read_cb_sd (uint32_t lba, void* buffer, uint32_t bufsize);
int32_t msc_write_cb_sd (uint32_t lba, uint8_t* buffer, uint32_t bufsize);
void msc_flush_cb_sd (void);
bool msc_start_stop_cb_sd(uint8_t power_condition, bool start, bool load_eject);
