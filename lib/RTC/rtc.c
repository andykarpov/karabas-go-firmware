/*
 Copyright (C) 2021-2023 Andy Karpov <andy.karpov@gmail.com>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */

#include "rtc.h"
#include "ds3231.h"
#include "eeprom24.h"
#include "rtc_regs.h"
#include "ms_time.h"

#define MSM_TIME_REGS 13
// the cpu writes the time one nibble at a time: the clock is set once the writes stop
#define MSM_WRITE_IDLE_MS 100

static rtc_send_t send_cb;
static rtc_event_t event_cb;
static bool is_started = false;
static bool has_eeprom = false;
static bool has_clock = false;

static uint8_t rtc_year = 0;
static uint8_t rtc_month = 0;
static uint8_t rtc_day = 1;
static uint8_t rtc_hours = 0;
static uint8_t rtc_minutes = 0;
static uint8_t rtc_seconds = 0;
static uint8_t rtc_seconds_alarm = 0;
static uint8_t rtc_minutes_alarm = 0;
static uint8_t rtc_hours_alarm = 0;
static uint8_t rtc_week = 1;

static uint8_t eeprom_bank = 0;
static uint8_t rtc_type = 0;

static int rtc_last_write_reg = 0;
static uint8_t rtc_last_write_data = 0;

// MSM6242B: the nibbles written by the core, not yet in the clock chip
static uint8_t msm_regs[MSM_TIME_REGS];
static bool msm_pending = false;
static uint32_t msm_write_ms = 0;

static bool rtc_is_bcd = false;
static bool rtc_is_24h = true;

static uint32_t tr = 0; // rtc poll time

static uint8_t time_to12h(uint8_t val) {
  return val > 12 ? val - 12 : val;
}

static uint8_t get_year(int year) {
  return (uint8_t) (year % 100);
}

static void send(uint8_t reg, uint8_t data) {
  send_cb(CMD_RTC, reg, data);
}

void rtc_begin(rtc_send_t s, rtc_event_t evt) {
  ms_delay(100);
  i2c_bus_lock();
  // init eeprom (24C08)
  has_eeprom = eeprom24_present();
  send_cb = s;
  event_cb = evt;
  has_clock = ds3231_present();
  if (has_clock && ds3231_lost_power()) {
    // keep a time that still reads as one (this clears OSF), else the build time
    kg_datetime_t t;
    if (ds3231_read(&t) || kg_datetime_parse_build(__DATE__, __TIME__, &t)) {
      ds3231_write(&t);
    }
  }
  i2c_bus_unlock();
  is_started = true;
}

bool rtc_started(void) { return is_started; }
bool rtc_has_clock(void) { return has_clock; }

bool rtc_now(kg_datetime_t *dt) {
  if (!has_clock || rtc_month == 0) return false; // no clock or not read yet
  *dt = (kg_datetime_t) { 2000 + rtc_year, rtc_month, rtc_day, rtc_hours, rtc_minutes, rtc_seconds };
  return true;
}

// the time fields into the clock chip; the core writes the registers one by one: a date that
// does not exist yet waits for the rest
static void adjust(void) {
  kg_datetime_t t = { 2000 + rtc_year, rtc_month, rtc_day, rtc_hours, rtc_minutes, rtc_seconds };
  if (!has_clock || !kg_datetime_valid(&t)) return;
  i2c_bus_lock();
  ds3231_write(&t);
  i2c_bus_unlock();
}

// the time from the clock chip into the fields; false if it could not be read
static bool read_clock(void) {
  if (!has_clock) return false;
  kg_datetime_t now;
  i2c_bus_lock();
  bool ok = ds3231_read(&now);
  i2c_bus_unlock();
  if (!ok) return false;
  rtc_year = get_year(now.year);
  rtc_month = now.month;
  rtc_day = now.day;
  rtc_week = kg_datetime_weekday(&now);
  rtc_hours = now.hour;
  rtc_minutes = now.minute;
  rtc_seconds = now.second;
  return true;
}

static void read_all(void);

static void msm_time_regs(uint8_t *regs) {
  uint8_t v[6] = { rtc_seconds, rtc_minutes, rtc_hours, rtc_day, rtc_month, rtc_year };
  for (uint8_t i = 0; i < 6; i++) {
    regs[2 * i] = v[i] % 10;
    regs[2 * i + 1] = (v[i] / 10) % 10;
  }
  regs[12] = rtc_week % 7; // 0 = sunday
}

static void send_time(void) {
  if (rtc_type == RTC_TYPE_MSM6242B) {
    uint8_t regs[MSM_TIME_REGS];
    msm_time_regs(regs);
    for (uint8_t i = 0; i < MSM_TIME_REGS; i++) send(i, regs[i]);
  } else if (rtc_type == RTC_TYPE_DS1307) {
    send(0, kg_bin2bcd(rtc_seconds));
    send(1, kg_bin2bcd(rtc_minutes));
    send(2, kg_bin2bcd(rtc_hours));
    send(3, kg_bin2bcd(rtc_week));
    send(4, kg_bin2bcd(rtc_day));
    send(5, kg_bin2bcd(rtc_month));
    send(6, kg_bin2bcd(rtc_year));
  } else {
    // mc146818a time registers
    send(0, rtc_is_bcd ? kg_bin2bcd(rtc_seconds) : rtc_seconds);
    send(2, rtc_is_bcd ? kg_bin2bcd(rtc_minutes) : rtc_minutes);
    uint8_t h = rtc_is_24h ? rtc_hours : time_to12h(rtc_hours);
    send(4, rtc_is_bcd ? kg_bin2bcd(h) : h);
    send(6, rtc_is_bcd ? kg_bin2bcd(rtc_week) : rtc_week);
    send(7, rtc_is_bcd ? kg_bin2bcd(rtc_day) : rtc_day);
    send(8, rtc_is_bcd ? kg_bin2bcd(rtc_month) : rtc_month);
    send(9, rtc_is_bcd ? kg_bin2bcd(rtc_year) : rtc_year);
  }
}

static void msm_commit(void) {
  msm_pending = false;
  uint8_t s = msm_regs[0] + 10 * msm_regs[1];
  uint8_t mi = msm_regs[2] + 10 * msm_regs[3];
  uint8_t h = msm_regs[4] + 10 * (msm_regs[5] & 3); // 24 h: bit 2 of H10 is PM in 12 h mode only
  uint8_t d = msm_regs[6] + 10 * msm_regs[7];
  uint8_t mo = msm_regs[8] + 10 * msm_regs[9];
  uint8_t y = msm_regs[10] + 10 * msm_regs[11]; // the Amiga takes 78-99 as 19xx; the chip has 20xx only
  if (s > 59 || mi > 59 || h > 23 || d < 1 || d > 31 || mo < 1 || mo > 12 || y > 99) {
    send_time(); // not a time: the core gets the clock back
    return;
  }
  rtc_seconds = s; rtc_minutes = mi; rtc_hours = h;
  rtc_day = d; rtc_month = mo; rtc_year = y;
  adjust();
  read_all(); // the day of the week of the new date
  send_time();
}

void rtc_handle(void) {
  uint32_t n = ms_now();
  // read time from rtc
  if (n - tr >= 500) {
    read_clock(); // keep the last good time if the rtc read failed
    // a time written by the core but not yet set: do not send the old one over it
    if (!msm_pending) send_time();
    event_cb();
    tr = n;
  }
  if (msm_pending && n - msm_write_ms >= MSM_WRITE_IDLE_MS) {
    msm_commit();
  }
}

void rtc_save(void) {
  adjust(); // the day of the week comes from the date
}

// the time, and is_bcd / is_24h from register B
static void read_all(void) {
  read_clock();
  uint8_t reg_b = rtc_get_eeprom_reg(0xB);
  if (rtc_type == RTC_TYPE_DS1307) {
    rtc_is_bcd = true;
  } else {
    rtc_is_bcd = (rtc_type == RTC_TYPE_ZXEVO) ? true : (reg_b >> 2) & 1;
  }
  rtc_is_24h = true;
}

void rtc_send_all(void) {
  // actualize time
  read_all();
  if (rtc_type == RTC_TYPE_MSM6242B) {
    // the time only: the control registers are in the core
    send_time();
  } else if (rtc_type == RTC_TYPE_DS1307) {
    send_time();
    // control register is always 0 (sqw, prescalers, etc)
    send(7, 0);
    // eeprom registers
    for (int reg = 8; reg < 64; reg++) send(reg, rtc_get_eeprom_reg(reg));
  } else {
    send_time();
    // control registers and eeprom
    for (int reg = 0; reg <= 255; reg++) {
      uint8_t data;
      switch (reg) {
        // alarms
        case 1: send(reg, rtc_is_bcd ? kg_bin2bcd(rtc_seconds_alarm) : rtc_seconds_alarm); break;
        case 3: send(reg, rtc_is_bcd ? kg_bin2bcd(rtc_minutes_alarm) : rtc_minutes_alarm); break;
        case 5: {
          uint8_t h = rtc_is_24h ? rtc_hours_alarm : time_to12h(rtc_hours_alarm);
          send(reg, rtc_is_bcd ? kg_bin2bcd(h) : h);
          break;
        }
        // control registers
        case 0xA: data = rtc_get_eeprom_reg(reg) & ~0x80; send(reg, data); break;
        case 0xB: data = rtc_get_eeprom_reg(reg) | 0x02; send(reg, data); break; // always 24h mode
        case 0xC: send(reg, 0x00); break;
        case 0xD: send(reg, 0x80); break;
        // eeprom (also over the time registers 0..9, as before: the next rtc_handle() sends
        // the time again)
        default: send(reg, rtc_get_eeprom_reg(reg));
      }
    }
  }
}

void rtc_set_data(uint8_t addr, uint8_t data) {
  if (rtc_type == RTC_TYPE_MSM6242B) {
    if (addr >= MSM_TIME_REGS) return;
    // the first write of a sequence starts from the current time: a program may set a few
    // registers only
    if (!msm_pending) msm_time_regs(msm_regs);
    msm_regs[addr] = data & 0x0F;
    msm_pending = true;
    msm_write_ms = ms_now();

  } else if (rtc_type == RTC_TYPE_DS1307) {
    rtc_last_write_reg = addr;
    rtc_last_write_data = data;
    // addressable only 64 registers
    addr &= 0x3F;
    switch (addr) {
      case 0: rtc_seconds = kg_bcd2bin(data); break;
      case 1: rtc_minutes = kg_bcd2bin(data); break;
      case 2: rtc_hours = kg_bcd2bin(data & 0x3F); break;
      case 3: rtc_week = kg_bcd2bin(data); break;
      case 4: rtc_day = kg_bcd2bin(data); break;
      case 5: rtc_month = kg_bcd2bin(data); break;
      case 6: rtc_year = kg_bcd2bin(data); break;
      case 7: rtc_set_eeprom_reg(addr, data & ~0x6C); break; // control register
      default: rtc_set_eeprom_reg(addr, data); // eeprom
    }
    // a write to a time register (0-6) sets the clock chip
    if (addr <= 6) adjust();

  } else {
    // skip multiple writes for clock registers
    if (rtc_last_write_reg == addr && rtc_last_write_data == data && addr <= 0xD) return;
    rtc_last_write_reg = addr;
    rtc_last_write_data = data;
    switch (addr) {
      case 0: rtc_seconds = rtc_is_bcd ? kg_bcd2bin(data) : data; break;
      case 1: rtc_seconds_alarm = rtc_is_bcd ? kg_bcd2bin(data) : data; break;
      case 2: rtc_minutes = rtc_is_bcd ? kg_bcd2bin(data) : data; break;
      case 3: rtc_minutes_alarm = rtc_is_bcd ? kg_bcd2bin(data) : data; break;
      case 4: rtc_hours = rtc_is_bcd ? kg_bcd2bin(data) : data; break;
      case 5: rtc_hours_alarm = rtc_is_bcd ? kg_bcd2bin(data) : data; break;
      case 6: rtc_week = rtc_is_bcd ? kg_bcd2bin(data) : data; break;
      case 7: rtc_day = rtc_is_bcd ? kg_bcd2bin(data) : data; break;
      case 8: rtc_month = rtc_is_bcd ? kg_bcd2bin(data) : data; break;
      case 9: rtc_year = rtc_is_bcd ? kg_bcd2bin(data) : data; break;
      case 0xA: rtc_set_eeprom_reg(addr, data & ~0x80); break;
      case 0xB:
        // zxevo always uses BCD; always 24 h; register B itself is not kept (tsconf
        // maxi-clock writes it too often)
        rtc_is_bcd = rtc_type == RTC_TYPE_ZXEVO ? true : (data >> 2) & 1;
        rtc_is_24h = true;
        break;
      case 0xC: // C and D are read-only registers
      case 0xD: break;
      default: rtc_set_eeprom_reg(addr, data);
    }
    if (addr <= 9) adjust();
  }
}

uint8_t rtc_get_week(void) { return rtc_week; }
uint8_t rtc_get_hour(void) { return rtc_hours; }
uint8_t rtc_get_minute(void) { return rtc_minutes; }
uint8_t rtc_get_second(void) { return rtc_seconds; }
uint8_t rtc_get_day(void) { return rtc_day; }
uint8_t rtc_get_month(void) { return rtc_month; }
uint8_t rtc_get_year(void) { return rtc_year; }
void rtc_set_week(uint8_t val) { rtc_week = val; }
void rtc_set_hour(uint8_t val) { rtc_hours = val; }
void rtc_set_minute(uint8_t val) { rtc_minutes = val; }
void rtc_set_second(uint8_t val) { rtc_seconds = val; }
void rtc_set_day(uint8_t val) { rtc_day = val; }
void rtc_set_month(uint8_t val) { rtc_month = val; }
void rtc_set_year(uint8_t val) { rtc_year = val; }
void rtc_set_eeprom_bank(uint8_t val) { eeprom_bank = val; }
void rtc_set_rtc_type(uint8_t val) { rtc_type = val; }

uint8_t rtc_get_eeprom_reg(uint8_t reg) {
  if (has_eeprom && eeprom_bank < 4) {
    i2c_bus_lock();
    uint8_t data = eeprom24_read((uint16_t) eeprom_bank * 256 + reg);
    i2c_bus_unlock();
    return data;
  } else if (eeprom_bank >= 4 && eeprom_bank < 255) {
    return core_eeprom_get(reg);
  }
  return 0xFF;
}

void rtc_set_eeprom_reg(uint8_t reg, uint8_t val) {
  if (has_eeprom && eeprom_bank < 4) {
    i2c_bus_lock();
    eeprom24_write((uint16_t) eeprom_bank * 256 + reg, val);
    i2c_bus_unlock();
  } else if (eeprom_bank >= 4 && eeprom_bank < 255) {
    core_eeprom_set(reg, val);
  }
  // 255 means no eeprom allowed
}
