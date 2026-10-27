/*
 Copyright (C) 2021-2023 Andy Karpov <andy.karpov@gmail.com>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */

#include <RTC.h>
#include <Arduino.h>
#include "ds3231.h"
#include "eeprom24.h"

// karabas: the I2C bus is shared between tasks; the firmware provides a recursive lock
// (src/periph/i2c_bus.cpp). It is held only around the I2C calls, never around action()
// (spi to the fpga) or event() callbacks.
extern "C" void i2c_bus_lock(void);
extern "C" void i2c_bus_unlock(void);
struct I2cHold {
  I2cHold() { i2c_bus_lock(); }
  ~I2cHold() { i2c_bus_unlock(); }
};

/****************************************************************************/

RTC::RTC(void)
{  
}

/****************************************************************************/

void RTC::begin(spi_cb act, osd_cb evt)
{
  delay(100);

  I2cHold i2c;

  // init eeprom (24C08)
  has_eeprom = eeprom24_present();

  // callbacks
  action = act;
  event = evt;

  has_clock = ds3231_present();

  if (has_clock && ds3231_lost_power()) {
    // keep a time that still reads as one (this clears OSF), else the build time
    kg_datetime_t t;
    if (ds3231_read(&t) || kg_datetime_parse_build(__DATE__, __TIME__, &t)) {
      ds3231_write(&t);
    }
  }
  is_started = true;
}

bool RTC::started()
{
  return is_started;
}

bool RTC::hasClock()
{
  return has_clock;
}

bool RTC::now(kg_datetime_t &dt)
{
  if (!has_clock || rtc_month == 0) return false; // no clock or not read yet
  dt = (kg_datetime_t) { 2000 + rtc_year, rtc_month, rtc_day, rtc_hours, rtc_minutes, rtc_seconds };
  return true;
}

void RTC::adjust()
{
  kg_datetime_t t = { 2000 + rtc_year, rtc_month, rtc_day, rtc_hours, rtc_minutes, rtc_seconds };
  // the core writes the registers one by one: a date that does not exist yet waits for the rest
  if (!has_clock || !kg_datetime_valid(&t)) return;
  I2cHold i2c;
  ds3231_write(&t);
}

// the time from the clock chip into the fields; false if it could not be read
static bool read_clock(bool has_clock, kg_datetime_t &t)
{
  I2cHold i2c;
  return has_clock && ds3231_read(&t);
}

void RTC::handle()
{
  unsigned long n = millis();

  // read time from rtc
  if (n - tr >= 500) {

    kg_datetime_t now;
    // keep the last good time if the rtc read failed
    if (read_clock(has_clock, now)) {
      rtc_year = get_year(now.year);
      rtc_month = now.month;
      rtc_day = now.day;
      rtc_week = kg_datetime_weekday(&now);

      rtc_hours = now.hour;
      rtc_minutes = now.minute;
      rtc_seconds = now.second;
    }

    // a time written by the core but not yet set: do not send the old one over it
    if (!msm_pending) sendTime();

    // cb rtc
    event();

    tr = n;
  }

  if (msm_pending && n - msm_write_ms >= MSM_WRITE_IDLE_MS) {
    msm_commit();
  }

  /*if (n - to >= 100) {
    event();
    to = n;
  }*/
}

void RTC::save() {
  adjust(); // the day of the week comes from the date
}

void RTC::send(uint8_t reg, uint8_t data) {
  action(CMD_RTC, reg, data);
}

void RTC::sendTime() {
  //d_printf("Time: %02d:%02d:%02d %02d-%02d-%02d", rtc_hours, rtc_minutes, rtc_seconds, rtc_day, rtc_month, rtc_year); d_println();

  if (rtc_type == RTC_TYPE_MSM6242B) {
    uint8_t regs[MSM_TIME_REGS];
    msm_time_regs(regs);
    for (uint8_t i = 0; i < MSM_TIME_REGS; i++) send(i, regs[i]);
  } else if (rtc_type == RTC_TYPE_DS1307) {
    // send ds1307 time registers
    send(0, bin2bcd(rtc_seconds));
    send(1, bin2bcd(rtc_minutes));
    send(2, bin2bcd(rtc_hours));
    send(3, bin2bcd(rtc_week));
    send(4, bin2bcd(rtc_day));
    send(5, bin2bcd(rtc_month));
    send(6, bin2bcd(rtc_year));
  } else {
    // send mc146818a time registers
    send(0, rtc_is_bcd ? bin2bcd(rtc_seconds) : rtc_seconds);
    send(2, rtc_is_bcd ? bin2bcd(rtc_minutes) : rtc_minutes);
    send(4, rtc_is_24h ? (rtc_is_bcd ? bin2bcd(rtc_hours) : rtc_hours) : (rtc_is_bcd ? bin2bcd(time_to12h(rtc_hours)) : time_to12h(rtc_hours)));
    send(6, rtc_is_bcd ? bin2bcd(rtc_week) : rtc_week);
    send(7, rtc_is_bcd ? bin2bcd(rtc_day) : rtc_day);
    send(8, rtc_is_bcd ? bin2bcd(rtc_month) : rtc_month);
    send(9, rtc_is_bcd ? bin2bcd(rtc_year) : rtc_year);
  }
}

void RTC::sendAll() {
  //d_println("RTC.sendAll()");

  // actualize time
  readAll();

  if (rtc_type == RTC_TYPE_MSM6242B) {
    // the time only: the control registers are in the core
    sendTime();
  } else if (rtc_type == RTC_TYPE_DS1307) {
    // time
    sendTime();
    // control register is always 0 (sqw, prescalers, etc)
    send(7, 0);
    // eeprom registers
    for (int reg = 8; reg < 64; reg++) {
      // eeprom
      uint8_t data = getEepromReg(reg);
      //Serial.printf("<== rtc reg %02x = %02x", reg, data); Serial.println();
      send(reg, data);
    }
  } else {
    sendTime();
    // control registers and eeprom
    uint8_t data;
    for (int reg = 0; reg <= 255; reg++) {
      switch (reg) {
        // alarms
        case 1: data = rtc_is_bcd ? bin2bcd(rtc_seconds_alarm) : rtc_seconds_alarm; send(reg,data); break;
        case 3: data = rtc_is_bcd ? bin2bcd(rtc_minutes_alarm) : rtc_minutes_alarm; send(reg,data); break;
        case 5: data = rtc_is_24h ? (rtc_is_bcd ? bin2bcd(rtc_hours_alarm) : rtc_hours_alarm) : (rtc_is_bcd ? bin2bcd(time_to12h(rtc_hours_alarm)) : time_to12h(rtc_hours_alarm)); send(reg,data); break;
        // control registers
        case 0xA: data = getEepromReg(reg); bitClear(data, 7); send(reg,data); break;
        case 0xB: data = getEepromReg(reg); bitSet(data, 1); send(reg,data); break; // always 24h mode
        case 0xC: data = 0x0; send(reg,data); break;
        case 0xD: data = 0x80; send(reg,data); break; // 10000000
        // eeprom
        default: data = getEepromReg(reg); send(reg, data);
      }
      //d_printf("%02x ", data); 
      //if ((reg > 0) && ((reg+1) % 16 == 0)) d_println();
    }
  }

  
}

void RTC::msm_time_regs(uint8_t *regs) {
  uint8_t v[6] = { rtc_seconds, rtc_minutes, rtc_hours, rtc_day, rtc_month, rtc_year };
  for (uint8_t i = 0; i < 6; i++) {
    regs[2 * i] = v[i] % 10;
    regs[2 * i + 1] = (v[i] / 10) % 10;
  }
  regs[12] = rtc_week % 7; // 0 = sunday
}

void RTC::msm_commit() {
  msm_pending = false;
  uint8_t s = msm_regs[0] + 10 * msm_regs[1];
  uint8_t mi = msm_regs[2] + 10 * msm_regs[3];
  uint8_t h = msm_regs[4] + 10 * (msm_regs[5] & 3); // 24 h: bit 2 of H10 is PM in 12 h mode only
  uint8_t d = msm_regs[6] + 10 * msm_regs[7];
  uint8_t mo = msm_regs[8] + 10 * msm_regs[9];
  uint8_t y = msm_regs[10] + 10 * msm_regs[11]; // the Amiga takes 78-99 as 19xx; the chip has 20xx only
  if (s > 59 || mi > 59 || h > 23 || d < 1 || d > 31 || mo < 1 || mo > 12 || y > 99) {
    sendTime(); // not a time: the core gets the clock back
    return;
  }
  rtc_seconds = s; rtc_minutes = mi; rtc_hours = h;
  rtc_day = d; rtc_month = mo; rtc_year = y;
  adjust();
  readAll(); // the day of the week of the new date
  sendTime();
}

void RTC::setData(uint8_t addr, uint8_t data) {

  if (rtc_type == RTC_TYPE_MSM6242B) {
    if (addr >= MSM_TIME_REGS) return;
    // the first write of a sequence starts from the current time: a program may set a few
    // registers only
    if (!msm_pending) msm_time_regs(msm_regs);
    msm_regs[addr] = data & 0x0F;
    msm_pending = true;
    msm_write_ms = millis();

  } else if (rtc_type == RTC_TYPE_DS1307) {

    // skip multiple writes for clock registers
    //if (rtc_last_write_reg == addr && rtc_last_write_data == data && addr <= 0x08) return;

    //Serial.printf("==> rtc reg %02x = %02x", addr, data); Serial.println();

      rtc_last_write_reg = addr;
      rtc_last_write_data = data;

        // addressable only 64 registers
        addr = bitClear(addr, 7);
        addr = bitClear(addr, 6);
        switch (addr) {
          case 0: rtc_seconds = bcd2bin(data); break;
          case 1: rtc_minutes = bcd2bin(data); break;
          case 2: data = bitClear(data, 7); data = bitClear(data, 6); rtc_hours = bcd2bin(data); break;
          case 3: rtc_week = bcd2bin(data); break;
          case 4: rtc_day = bcd2bin(data); break;
          case 5: rtc_month = bcd2bin(data); break;
          case 6: rtc_year = bcd2bin(data); break;
          case 7: data = bitClear(data,6); // control register
                  data = bitClear(data, 5); 
                  data = bitClear(data, 3); 
                  data = bitClear(data, 2);
                  setEepromReg(addr, data);
                  break;
          default: setEepromReg(addr, data); // eeprom
        }

        // a write to a time register (0-6) sets the clock chip
        if (addr <= 6) {
            adjust();
        }

  } else {

    // skip multiple writes for clock registers
    if (rtc_last_write_reg == addr && rtc_last_write_data == data && addr <= 0xD) return;

    if (addr != 0x0C && addr != 0x0D && addr < 0x0A) {
      //Serial.printf("RTC %02x => %02x", addr, data); Serial.println();
    }

      rtc_last_write_reg = addr;
      rtc_last_write_data = data;
      uint8_t prev;

      switch (addr) {
        case 0: rtc_seconds = rtc_is_bcd ? bcd2bin(data) : data; break;
        case 1: rtc_seconds_alarm = rtc_is_bcd ? bcd2bin(data) : data; break;
        case 2: rtc_minutes = rtc_is_bcd ? bcd2bin(data) : data; break;
        case 3: rtc_minutes_alarm = rtc_is_bcd ? bcd2bin(data) : data;  break;
        case 4: rtc_hours = rtc_is_bcd ? bcd2bin(data) : data; break;
        case 5: rtc_hours_alarm = rtc_is_bcd ? bcd2bin(data) : data; break;
        case 6: rtc_week = rtc_is_bcd ? bcd2bin(data) : data; break;
        case 7: rtc_day = rtc_is_bcd ? bcd2bin(data) : data; break;
        case 8: rtc_month = rtc_is_bcd ? bcd2bin(data) : data; break;
        case 9: rtc_year = (rtc_is_bcd ? bcd2bin(data) : data); break;
        case 0xA: bitClear(data, 7); setEepromReg(addr, data); break;
        case 0xB: 
                  if (rtc_type == RTC_TYPE_ZXEVO) {
                    bitClear(data, 2);
                    rtc_is_bcd = true; // zxevo always uses BCD
                  } else {
                    rtc_is_bcd = bitRead(data, 2);
                  } 
                  rtc_is_24h = true; //bitRead(data, 1);  
                  bitSet(data, 1); // always 24-h format
                  bitClear(data, 7);
                  // tsconf maxi-clock uses regB too much
                  /*prev = getEepromReg(addr);
                  if (prev != data) {
                    setEepromReg(addr, data); 
                  }*/
                  break;
        case 0xC: // C and D are read-only registers
        case 0xD: break;
        default: setEepromReg(addr, data);
      }

      if (addr <= 9) {
          adjust();
      }
  }
}

void RTC::readAll() {

  kg_datetime_t now;
  if (read_clock(has_clock, now)) {
    rtc_year = get_year(now.year);
    rtc_month = now.month;
    rtc_day = now.day;
    rtc_week = kg_datetime_weekday(&now);

    rtc_hours = now.hour;
    rtc_minutes = now.minute;
    rtc_seconds = now.second;
  }

  // read is_bcd, is_24h
  uint8_t reg_b = getEepromReg(0xB);
  if (rtc_type == RTC_TYPE_DS1307) {
    rtc_is_bcd = true;
  } else {
    rtc_is_bcd = (rtc_type == RTC_TYPE_ZXEVO) ? true : bitRead(reg_b, 2);
  }
  rtc_is_24h = true; //bitRead(reg_b, 1);
}

uint8_t RTC::getWeek() {
  return rtc_week;
}

uint8_t RTC::getHour() {
  return rtc_hours;
}

uint8_t RTC::getMinute() {
  return rtc_minutes;
}

uint8_t RTC::getSecond() {
  return rtc_seconds;
}

uint8_t RTC::getDay() {
  return rtc_day;
}

uint8_t RTC::getMonth() {
  return rtc_month;
}

uint8_t RTC::getYear() {
  return rtc_year;
}

void RTC::setWeek(uint8_t val) {
  rtc_week = val;
}

void RTC::setHour(uint8_t val) {
  rtc_hours = val;
}

void RTC::setMinute(uint8_t val) {
  rtc_minutes = val;
}

void RTC::setSecond(uint8_t val) {
  rtc_seconds = val;
}

void RTC::setDay(uint8_t val) {
  rtc_day = val;
}

void RTC::setMonth(uint8_t val) {
  rtc_month = val;
}

void RTC::setYear(uint8_t val) {
  rtc_year = val;
}

void RTC::setEepromBank(uint8_t val) {
  eeprom_bank = val;
}

void RTC::setRtcType(uint8_t val) {
  rtc_type = val;
}

uint8_t RTC::getEepromReg(uint8_t reg) {
  if (has_eeprom && eeprom_bank < 4) {
    uint32_t addr = (uint32_t)eeprom_bank*256 + reg;
    uint8_t data;
    { I2cHold i2c; data = eeprom24_read(addr); }
    //Serial.printf("<== eeprom %08x = %02x", addr, data); Serial.println();
    return data;
  } else if (eeprom_bank >=4 && eeprom_bank < 255) {
    return core_eeprom_get(reg);
  } else {
    return 0xFF;
  }
}

void RTC::setEepromReg(uint8_t reg, uint8_t val) {
  //d_printf("Set eeprom reg %02x = %02x", reg, val); d_println();
  if (has_eeprom && eeprom_bank < 4) {
    uint32_t addr = (uint32_t)eeprom_bank*256 + reg;
    //Serial.printf("==> eeprom %08x = %02x", addr, val); Serial.println();
    { I2cHold i2c; eeprom24_write(addr, val); }
  } else if (eeprom_bank >= 4 && eeprom_bank < 255) {
    core_eeprom_set(reg, val);
  } else {
    // noting here, 255 means no eeprom allowed
  }
}

uint8_t RTC::bin2bcd(uint8_t val) {
  return val + 6 * (val / 10);
}

uint8_t RTC::bcd2bin(uint8_t val) {
  return val - 6 * (val >> 4);
}

uint8_t RTC::time_to24h(uint8_t val) {
  bool pm = bitRead(val, 7);
  bitClear(val, 7);
  val = (pm) ? val + 12 : val;
  return (val < 23) ? val : 0;
}

uint8_t RTC::time_to12h(uint8_t val) {
  bool pm = false;
  if (val > 12) {
    val = val - 12;
    pm = true;
  }
  //bitWrite(val, 7, pm);
  return val;
}

uint8_t RTC::get_year(int year) {
  int res = year % 100;
  return lowByte(res);
}



/****************************************************************************/

RTC rtc = RTC();

// vim:cin:ai:sts=2 sw=2 ft=cpp
