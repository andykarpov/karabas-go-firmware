/*
 Copyright (C) 2021-2023 Andy Karpov <andy.karpov@gmail.com>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */

#include <SPI.h>
#include <RTC.h>
#include <Arduino.h>
#include <utils.h>

/****************************************************************************/

RTC::RTC(void)
{  
}

/****************************************************************************/

void RTC::begin(spi_cb act, osd_cb evt)
{
  // init eeprom
  eeprom.setMemoryType(8);
  has_eeprom = eeprom.begin();

  // callbacks
  action = act;
  event = evt;

  rtc_clock.setClockMode(false); // 24h format always

  // run the clock in case of OFS flag is on
  //if (!rtc_clock.oscillatorCheck()) {
  //  rtc_clock.setSecond(0);
  //}

  readAll();
  is_started = true;
}

bool RTC::started()
{
  return is_started;
}

void RTC::handle()
{
  unsigned long n = millis();

  // read time from rtc
  if (n - tr >= 500) {

    DateTime now = rtc_lib.now();

    rtc_year = get_year(now.getYear());
    rtc_month = now.getMonth();
    rtc_day = now.getDay();
    rtc_week = now.getWeekDay();

    rtc_hours = now.getHour();
    rtc_minutes = now.getMinute();
    rtc_seconds = now.getSecond();

    sendTime();

    // cb rtc
    event();

    tr = n;
  }
}

void RTC::save() {
  rtc_clock.setClockMode(false); // 24h always
  rtc_clock.setDate(rtc_day);
  rtc_clock.setMonth(rtc_month);
  rtc_clock.setYear(rtc_year);
  rtc_clock.setDoW(rtc_week);
  rtc_clock.setHour(rtc_hours);
  rtc_clock.setMinute(rtc_minutes);
  rtc_clock.setSecond(rtc_seconds);
}

void RTC::send(uint8_t reg, uint8_t data) {
  action(CMD_RTC, reg, data);
}

void RTC::sendTime() {
  //d_printf("Time: %02d:%02d:%02d %02d-%02d-%02d", rtc_hours, rtc_minutes, rtc_seconds, rtc_day, rtc_month, rtc_year); d_println();
  send(0, rtc_is_bcd ? bin2bcd(rtc_seconds) : rtc_seconds);
  send(2, rtc_is_bcd ? bin2bcd(rtc_minutes) : rtc_minutes);
  send(4, rtc_is_24h ? (rtc_is_bcd ? bin2bcd(rtc_hours) : rtc_hours) : (rtc_is_bcd ? bin2bcd(time_to12h(rtc_hours)) : time_to12h(rtc_hours)));
  send(6, rtc_is_bcd ? bin2bcd(rtc_week) : rtc_week);
  send(7, rtc_is_bcd ? bin2bcd(rtc_day) : rtc_day);
  send(8, rtc_is_bcd ? bin2bcd(rtc_month) : rtc_month);
  send(9, rtc_is_bcd ? bin2bcd(rtc_year) : rtc_year);
}

void RTC::sendAll() {
  //d_println("RTC.sendAll()");
  uint8_t data;
  for (int reg = 0; reg <= 255; reg++) {
    switch (reg) {
      case 0: data = rtc_is_bcd ? bin2bcd(rtc_seconds) : rtc_seconds; break;
      case 1: data = rtc_is_bcd ? bin2bcd(rtc_seconds_alarm) : rtc_seconds_alarm; break;
      case 2: data = rtc_is_bcd ? bin2bcd(rtc_minutes) : rtc_minutes; break;
      case 3: data = rtc_is_bcd ? bin2bcd(rtc_minutes_alarm) : rtc_minutes_alarm; break;
      case 4: data = rtc_is_24h ? (rtc_is_bcd ? bin2bcd(rtc_hours) : rtc_hours) : (rtc_is_bcd ? bin2bcd(time_to12h(rtc_hours)) : time_to12h(rtc_hours)); break;
      case 5: data = rtc_is_24h ? (rtc_is_bcd ? bin2bcd(rtc_hours_alarm) : rtc_hours_alarm) : (rtc_is_bcd ? bin2bcd(time_to12h(rtc_hours_alarm)) : time_to12h(rtc_hours_alarm)); break;
      case 6: data = rtc_is_bcd ? bin2bcd(rtc_week) : rtc_week; break;
      case 7: data = rtc_is_bcd ? bin2bcd(rtc_day) : rtc_day; break;
      case 8: data = rtc_is_bcd ? bin2bcd(rtc_month) : rtc_month; break;
      case 9: data = rtc_is_bcd ? bin2bcd(rtc_year) : rtc_year; break;
      case 0xA: data = getEepromReg(reg); bitClear(data, 7); break;
      case 0xB: data = getEepromReg(reg); bitSet(data, 1); break; // always 24h mode
      case 0xC: data = 0x0; break;
      case 0xD: data = 0x80; break; // 10000000
      default: data = getEepromReg(reg); send(reg, data);
    }
    send(reg,data);
    //d_printf("%02x ", data); 
    //if ((reg > 0) && ((reg+1) % 16 == 0)) d_println();
  }
}

void RTC::setData(uint8_t addr, uint8_t data) {
  // skip multiple writes for clock registers
  if (rtc_last_write_reg == addr && rtc_last_write_data == data && addr <= 0xD) return;

   //if (addr != 0x0C && addr != 0x0D && addr < 0x0A) {
   //  d_printf("Set rtc reg %02x = %02x", addr, data); d_println();
   //}

    rtc_last_write_reg = addr;
    rtc_last_write_data = data;
    uint8_t prev;

    switch (addr) {
      case 0: rtc_seconds = rtc_is_bcd ? bcd2bin(data) : data; rtc_clock.setSecond(rtc_seconds); break;
      case 1: rtc_seconds_alarm = rtc_is_bcd ? bcd2bin(data) : data; break;
      case 2: rtc_minutes = rtc_is_bcd ? bcd2bin(data) : data; rtc_clock.setMinute(rtc_minutes); break;
      case 3: rtc_minutes_alarm = rtc_is_bcd ? bcd2bin(data) : data;  break;
      case 4: rtc_hours = rtc_is_bcd ? bcd2bin(data) : data; rtc_clock.setClockMode(false); rtc_clock.setHour(rtc_hours); break;
      case 5: rtc_hours_alarm = rtc_is_bcd ? bcd2bin(data) : data; break;
      case 6: rtc_week = rtc_is_bcd ? bcd2bin(data) : data; rtc_clock.setDoW(rtc_week); break;
      case 7: rtc_day = rtc_is_bcd ? bcd2bin(data) : data; rtc_clock.setDate(rtc_day); break;
      case 8: rtc_month = rtc_is_bcd ? bcd2bin(data) : data; rtc_clock.setMonth(rtc_month); break;
      case 9: rtc_year = (rtc_is_bcd ? bcd2bin(data) : data); rtc_clock.setYear(rtc_year); break;
      case 0xA: bitClear(data, 7); setEepromReg(addr, data); break;
      case 0xB: rtc_is_bcd = !bitRead(data, 2); 
                rtc_is_24h = true; //bitRead(data, 1);  
                bitSet(data, 1); // always 24-h format
                bitClear(data, 7);
                prev = getEepromReg(addr);
                if (prev != data) {
                  setEepromReg(addr, data); 
                }
                break;
      case 0xC: // C and D are read-only registers
      case 0xD: break;
      default: setEepromReg(addr, data);
    }
}

void RTC::readAll() {

  DateTime now = rtc_lib.now();

  rtc_year = get_year(now.getYear());
  rtc_month = now.getMonth();
  rtc_day = now.getDay();
  rtc_week = now.getWeekDay();

  rtc_hours = now.getHour();
  rtc_minutes = now.getMinute();
  rtc_seconds = now.getSecond();

  // read is_bcd, is_24h
  uint8_t reg_b = getEepromReg(0xB);
  rtc_is_bcd = !bitRead(reg_b, 2);
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

uint8_t RTC::getEepromReg(uint8_t reg) {
  if (has_eeprom && eeprom_bank < 4) {
    return eeprom.read(eeprom_bank*256 + reg);
  } else if (eeprom_bank >=4 && eeprom_bank < 255) {
    return core_eeprom_get(reg);
  } else {
    return 0xFF;
  }
}

void RTC::setEepromReg(uint8_t reg, uint8_t val) {
  //d_printf("Set eeprom reg %02x = %02x", reg, val); d_println();
  if (has_eeprom && eeprom_bank < 4) {
    eeprom.put(eeprom_bank*256 + reg, val);
  } else if (eeprom_bank >= 4 && eeprom_bank < 255) {
    core_eeprom_set(reg, val);
  } else {
    // noting here, 255 means no eeprom allowed
  }
}


/****************************************************************************/

RTC rtc = RTC();

// vim:cin:ai:sts=2 sw=2 ft=cpp
