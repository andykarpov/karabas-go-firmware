/*
 Copyright (C) 2024 Andy Karpov <andy.karpov@gmail.com>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */

// STL headers
// C headers
// Framework headers
// Library headers
#include <SPI.h>
// Project headers
// This component's header
#include <FT812.h>
#include <Arduino.h>

/****************************************************************************/

// Commands

const uint8_t CMD_FT812 = 0x09;
const uint8_t CMD_FT812_DATA = 0x0A;

const uint8_t ADDR_EXCLUSIVE = 0x00;

const uint8_t ADDR_CMD_BYTE1 = 0x01;
const uint8_t ADDR_CMD_BYTE2 = 0x02;
const uint8_t ADDR_CMD_BYTE3 = 0x03;

const uint8_t ADDR_TRANSACTION_LEN = 0x04;
const uint8_t ADDR_ADDRESS_BYTE1 = 0x05;
const uint8_t ADDR_ADDRESS_BYTE2 = 0x06;
const uint8_t ADDR_ADDRESS_BYTE3 = 0x07;
const uint8_t ADDR_ADDRESS_BYTE4 = 0x08;

const uint8_t ADDR_ACK_CMD = 0x09;
const uint8_t ADDR_ACK_DATA = 0x0A;

const uint8_t ADDR_DATA_BYTE = 0x00; // ... + ADDR_TRANSACTION_LEN-1 for CMD_FT812_DATA

/****************************************************************************/

FT812::FT812(void)
{
}

/****************************************************************************/

void FT812::begin(m_cb act)
{
  action = act;
}

void FT812::exclusive(bool spi_on, bool vga_on)
{
  uint8_t data = 0;
  if (spi_on) data = bitSet(data, 0); // set exclusive spi mode to ft812
  if (vga_on) data = bitSet(data, 1); // switch buffers to out vga from ft812
  action(CMD_FT812, ADDR_EXCLUSIVE, data);
}

void FT812::reset()
{
  exclusive(true, false);
  command(0x68, 0, 0);
  exclusive(false, false);
}

void FT812::command(uint8_t cmd1, uint8_t cmd2, uint8_t cmd3)
{
  Serial.printf("FT cmd %02x%02x%02x", cmd1, cmd2, cmd3); Serial.println();
  transaction_active = true;
  data_transaction = false;
  action(CMD_FT812, ADDR_CMD_BYTE1, cmd1);
  action(CMD_FT812, ADDR_CMD_BYTE2, cmd2);
  action(CMD_FT812, ADDR_CMD_BYTE3, cmd3);
}

void FT812::write(uint32_t addr, uint8_t *data, uint8_t len)
{
  Serial.printf("FT write %06x len %02x", addr, len); Serial.println();

  transaction_active = true;
  data_transaction = true;
  transaction_len = len;
  uint8_t addr3 = (uint8_t)((addr & 0x00FF0000) >> 16); 
  addr3 = bitSet(addr3, 7);  // write is "10"
  addr3 = bitClear(addr3, 6);
  uint8_t addr2 = (uint8_t)((addr & 0x0000FF00) >> 8);
  uint8_t addr1 = (uint8_t)((addr & 0x000000FF));  
  action(CMD_FT812, ADDR_TRANSACTION_LEN, len);
  action(CMD_FT812, ADDR_ADDRESS_BYTE1, addr3);
  action(CMD_FT812, ADDR_ADDRESS_BYTE2, addr2);
  action(CMD_FT812, ADDR_ADDRESS_BYTE3, addr1);
  action(CMD_FT812, ADDR_ADDRESS_BYTE4, 0); // dummy byte
  // send data
  for (uint8_t i = 0; i<len; i++) {
    action(CMD_FT812_DATA, i, data[i]);
  }
}

void FT812::read(uint32_t addr, uint8_t len) 
{
  Serial.printf("FT read %06x len %02x", addr, len); Serial.println();

  transaction_active = true;
  data_transaction = true;
  transaction_len = len;
  uint8_t addr3 = (uint8_t)((addr & 0x00FF0000) >> 16); 
  addr3 = bitClear(addr3, 7); // read is "00"
  addr3 = bitClear(addr3, 6);
  uint8_t addr2 = (uint8_t)((addr & 0x0000FF00) >> 8);
  uint8_t addr1 = (uint8_t)((addr & 0x000000FF)); 
  action(CMD_FT812, ADDR_TRANSACTION_LEN, len);
  action(CMD_FT812, ADDR_ADDRESS_BYTE1, addr3);
  action(CMD_FT812, ADDR_ADDRESS_BYTE2, addr2);
  action(CMD_FT812, ADDR_ADDRESS_BYTE3, addr1);
  action(CMD_FT812, ADDR_ADDRESS_BYTE4, 0); // dummy byte
   for (uint8_t i = 0; i<len; i++) {
    action(CMD_FT812_DATA, i, 0); // to read a byte we have to send a dummy byte
  }
}

void FT812::setData(uint8_t pos, uint8_t val)
{
  uint8_t count_system = (data_transaction) ? 4 : 3;
  uint8_t count_bytes = (data_transaction) ? count_system + transaction_len : count_system;

  //Serial.printf("Pos %02d, Sys %02d, Total %02d", pos, count_system, count_bytes); Serial.println();

  // skip system regs
  if (pos >= count_system) {
    buf[pos-count_system] = val;
  }

  // deactivate transaction progress on last byte received
  if (pos >= count_bytes-1) {
    transaction_active = false;
  }
}

uint8_t FT812::getData(uint8_t pos)
{
  return buf[pos];
}

void FT812::wait()
{
  while (transaction_active) {
    action(0xFF, 0, 0); // NOP
  }
}

// vim:cin:ai:sts=2 sw=2 ft=cpp
