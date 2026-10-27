/*
 Copyright (C) 2021-2023 Andy Karpov <andy.karpov@gmail.com>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */
#pragma once

// The RTC of the cores: the DS3231 and the 24C08 EEPROM (lib/I2cDevices) seen through the
// registers of the clock chip a core expects (MC146818A, ZX Evo, DS1307, MSM6242B), CMD_RTC
// frames both ways. The I2C bus lock is taken around the I2C calls only, never around the
// send / event callbacks. Pure C.

#include <stdbool.h>
#include <stdint.h>
#include "time_zone.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CMD_RTC 0xFA
#define RTC_TYPE_MC146818A 0
#define RTC_TYPE_DS1307 1
#define RTC_TYPE_ZXEVO 2
// MSM6242B (Amiga A500 / A501 at $DC0000): registers 0-12 are BCD nibbles S1 S10 MI1 MI10 H1 H10
// D1 D10 MO1 MO10 Y1 Y10 W, 24 h. The core keeps D, E, F (control) itself.
#define RTC_TYPE_MSM6242B 3

typedef void (*rtc_send_t)(uint8_t cmd, uint8_t addr, uint8_t data);
typedef void (*rtc_event_t)(void);

// the EEPROM of eeprom banks 4..254: in the core file (the firmware has them)
uint8_t core_eeprom_get(uint8_t pos);
void core_eeprom_set(uint8_t pos, uint8_t val);
// the I2C bus lock (src/periph/i2c_bus)
void i2c_bus_lock(void);
void i2c_bus_unlock(void);

void rtc_begin(rtc_send_t send, rtc_event_t event);
bool rtc_started(void);
bool rtc_has_clock(void);              // the DS3231 answered at rtc_begin()
bool rtc_now(kg_datetime_t *dt);       // the last time read, false without a clock
void rtc_handle(void);                 // reads the clock every 500 ms, sends it to the core
void rtc_save(void);                   // the time fields into the clock chip
void rtc_send_all(void);               // the time and the registers to the core
void rtc_set_data(uint8_t addr, uint8_t data); // a register written by the core

uint8_t rtc_get_week(void);
uint8_t rtc_get_hour(void);
uint8_t rtc_get_minute(void);
uint8_t rtc_get_second(void);
uint8_t rtc_get_day(void);
uint8_t rtc_get_month(void);
uint8_t rtc_get_year(void);           // 0..99
void rtc_set_week(uint8_t val);
void rtc_set_hour(uint8_t val);
void rtc_set_minute(uint8_t val);
void rtc_set_second(uint8_t val);
void rtc_set_day(uint8_t val);
void rtc_set_month(uint8_t val);
void rtc_set_year(uint8_t val);

void rtc_set_eeprom_bank(uint8_t val);
void rtc_set_rtc_type(uint8_t val);
uint8_t rtc_get_eeprom_reg(uint8_t reg);
void rtc_set_eeprom_reg(uint8_t reg, uint8_t val);

#ifdef __cplusplus
}
#endif
