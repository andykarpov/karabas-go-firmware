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

extern uint8_t osd_state;
extern core_item_t core;

extern bool is_osd_hiding;
extern ElapsedTimer hide_timer; 

void spi_queue(uint8_t cmd, uint8_t addr, uint8_t data);
void spi_send(uint8_t cmd, uint8_t addr, uint8_t data);
void process_in_cmd(uint8_t cmd, uint8_t addr, uint8_t data);

void do_configure(const char* filename);
uint32_t fpga_configure(const char* filename);
void halt(const char* msg);

void file_seek(uint32_t pos, bool is_flash);
uint8_t file_read(bool is_flash);
size_t file_read_bytes(char *buf, size_t len, bool is_flash);
int file_read_buf(char *buf, size_t len, bool is_flash);
uint16_t file_read16(uint32_t pos, bool is_flash);
uint32_t file_read24(uint32_t pos, bool is_flash);
uint32_t file_read32(uint32_t pos, bool is_flash);
void file_get_name(char *buf, size_t len, bool is_flash);
bool is_flashfs(const char* filename);

void osd_print_header();
void osd_print_footer();
void osd_handle(bool force);

void read_core(const char* filename);
void read_roms(const char* filename);
void core_trigger(uint8_t pos);
void core_send(uint8_t pos);

void on_time();
void on_keyboard();
