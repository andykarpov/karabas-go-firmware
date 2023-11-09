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
  if (eeprom.begin() == false) {
    Serial.println("No EEPROM memory detected.");
  } else {
    Serial.print("I2C EEPROM detected. Size in bytes: ");
    Serial.println(eeprom.length());
  }

  Serial.print("I2C DS3231 RTC detected. Temperature: ");
  Serial.println(rtc_clock.getTemperature());

  action = act;
  event = evt;

  readAll();
  sendTime();
  if (!getInitDone()) {
    sendAll();
  }
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

    bool century;
    rtc_year = rtc_clock.getYear();
    rtc_month = rtc_clock.getMonth(century);
    rtc_day = rtc_clock.getDate();
    rtc_week = rtc_clock.getDoW();

    bool h12, pm;
    rtc_hours = rtc_clock.getHour(h12, pm);
    rtc_minutes = rtc_clock.getMinute();
    rtc_seconds = rtc_clock.getSecond();

    sendTime();

    // cb rtc
    event();

    tr = n;
  }
}

void RTC::save() {
  rtc_clock.setClockMode(false);
  rtc_clock.setDate(rtc_day);
  rtc_clock.setMonth(rtc_month);
  rtc_clock.setYear(rtc_year);
  rtc_clock.setDoW(rtc_week);
  rtc_clock.setHour(rtc_hours);
  rtc_clock.setMinute(rtc_minutes);
  rtc_clock.setSecond(rtc_seconds);
}

void RTC::fixInvalidTime() {
  if (rtc_day < 1 || rtc_day > 31) rtc_day = 1;
  if (rtc_month < 1 || rtc_month > 12) rtc_month = 1;
  if (rtc_year < 2000 || rtc_year > 4095) rtc_year = 2000;
  if (rtc_hours > 23) rtc_hours = 0;
  if (rtc_minutes > 59) rtc_minutes = 0;
  if (rtc_seconds > 59) rtc_seconds = 0;
  if (rtc_week < 1 || rtc_week > 7) rtc_week = 1;
  save();
}

void RTC::send(uint8_t reg, uint8_t data) {
  action(CMD_RTC, reg, data);
}

void RTC::sendTime() {
  send(0, rtc_is_bcd ? bin2bcd(rtc_seconds) : rtc_seconds);
  send(2, rtc_is_bcd ? bin2bcd(rtc_minutes) : rtc_minutes);
  send(4, rtc_is_24h ? (rtc_is_bcd ? bin2bcd(rtc_hours) : rtc_hours) : (rtc_is_bcd ? bin2bcd(time_to12h(rtc_hours)) : time_to12h(rtc_hours)));
  send(6, rtc_is_bcd ? bin2bcd(rtc_week) : rtc_week);
  send(7, rtc_is_bcd ? bin2bcd(rtc_day) : rtc_day);
  send(8, rtc_is_bcd ? bin2bcd(rtc_month) : rtc_month);
  send(9, rtc_is_bcd ? bin2bcd(get_year(rtc_year)) : get_year(rtc_year));
}

void RTC::sendAll() {
  uint8_t data;
  for (int reg = 0; reg <= 255; reg++) {
    switch (reg) {
      case 0: send(reg, rtc_is_bcd ? bin2bcd(rtc_seconds) : rtc_seconds); break;
      case 1: send(reg, rtc_is_bcd ? bin2bcd(rtc_seconds_alarm) : rtc_seconds_alarm); break;
      case 2: send(reg, rtc_is_bcd ? bin2bcd(rtc_minutes) : rtc_minutes); break;
      case 3: send(reg, rtc_is_bcd ? bin2bcd(rtc_minutes_alarm) : rtc_minutes_alarm); break;
      case 4: send(reg, rtc_is_24h ? (rtc_is_bcd ? bin2bcd(rtc_hours) : rtc_hours) : (rtc_is_bcd ? bin2bcd(time_to12h(rtc_hours)) : time_to12h(rtc_hours))); break;
      case 5: send(reg, rtc_is_24h ? (rtc_is_bcd ? bin2bcd(rtc_hours_alarm) : rtc_hours_alarm) : (rtc_is_bcd ? bin2bcd(time_to12h(rtc_hours_alarm)) : time_to12h(rtc_hours_alarm))); break;
      case 6: send(reg, rtc_is_bcd ? bin2bcd(rtc_week) : rtc_week); break;
      case 7: send(reg, rtc_is_bcd ? bin2bcd(rtc_day) : rtc_day); break;
      case 8: send(reg, rtc_is_bcd ? bin2bcd(rtc_month) : rtc_month); break;
      case 9: send(reg, rtc_is_bcd ? bin2bcd(get_year(rtc_year)) : get_year(rtc_year)); break;
      case 0xA: data = eeprom.read(EEPROM_RTC_OFFSET + reg); bitClear(data, 7); send(reg, data); break;
      case 0xB: data = eeprom.read(EEPROM_RTC_OFFSET + reg); send(reg, data); break;
      case 0xC: send(reg, 0x0); break;
      case 0xD: send(reg, 0x80); break;
      default: send(reg, eeprom.read(EEPROM_RTC_OFFSET + reg));
      //default: send(reg, reg); // test!!!
    }
  }
}

void RTC::setData(uint8_t addr, uint8_t data) {
  // skip multiple writes for clock registers
  if (rtc_last_write_reg == addr && rtc_last_write_data == data && addr <= 0xD) return;

    rtc_last_write_reg = addr;
    rtc_last_write_data = data;

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
      case 9: rtc_year = 2000 + (rtc_is_bcd ? bcd2bin(data) : data); rtc_clock.setYear(rtc_year); break;
      case 0xA: bitClear(data, 7); eeprom.put(EEPROM_RTC_OFFSET + addr, data); break;
      case 0xB: rtc_is_bcd = !bitRead(data, 2); rtc_is_24h = bitRead(data, 1); eeprom.put(EEPROM_RTC_OFFSET + addr, data); break;
      case 0xC: // C and D are read-only registers
      case 0xD: break;
      default: eeprom.put(EEPROM_RTC_OFFSET + addr, data);
    }
}

void RTC::readAll() {
  bool century;
  rtc_year = rtc_clock.getYear();
  rtc_month = rtc_clock.getMonth(century);
  rtc_day = rtc_clock.getDate();
  rtc_week = rtc_clock.getDoW();

  bool h12, pm;
  rtc_hours = rtc_clock.getHour(h12, pm);
  rtc_minutes = rtc_clock.getMinute();
  rtc_seconds = rtc_clock.getSecond();

  // read is_bcd, is_24h
  uint8_t reg_b = eeprom.read(EEPROM_RTC_OFFSET + 0xB);
  rtc_is_bcd = !bitRead(reg_b, 2);
  rtc_is_24h = bitRead(reg_b, 1);
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

int RTC::getYear() {
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

void RTC::setYear(int val) {
  rtc_year = val;
}

  bool RTC::getInitDone() {
    return rtc_init_done;
  }

  void RTC::setInitDone(bool val) {
    rtc_init_done = val;
  }

bool RTC::getTimeIsValid() {
  return (rtc_hours <= 23 && rtc_minutes <= 59 && rtc_seconds <= 59);
}

bool RTC::getDateIsValid() {
  return (rtc_year < 9999 && rtc_month <= 12 && rtc_day <= 31 && rtc_week <= 7);
}

/****************************************************************************/

RTC rtc = RTC();

// vim:cin:ai:sts=2 sw=2 ft=cpp
