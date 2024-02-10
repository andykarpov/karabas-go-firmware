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

void FT812::exclusive(bool on, bool ft)
{
  uint8_t data = 0;
  if (on) data = bitSet(data, 0); // set exclusive spi mode to ft812
  if (ft) data = bitSet(data, 1); // switch buffers to out vga from ft812
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
  action(CMD_FT812, ADDR_CMD_BYTE1, cmd1);
  action(CMD_FT812, ADDR_CMD_BYTE2, cmd2);
  action(CMD_FT812, ADDR_CMD_BYTE3, cmd3);
}

void FT812::write(uint32_t addr, uint8_t *data, uint8_t len)
{
  transaction_active = true;
  transaction_len = len;
  action(CMD_FT812, ADDR_TRANSACTION_LEN, len);
  action(CMD_FT812, ADDR_ADDRESS_BYTE1, addr); // todo
  action(CMD_FT812, ADDR_ADDRESS_BYTE2, addr);
  action(CMD_FT812, ADDR_ADDRESS_BYTE3, addr);
  action(CMD_FT812, ADDR_ADDRESS_BYTE4, 0); // dummy byte
  // todo: send data
  transaction_active = false;
}

void FT812::read(uint32_t addr, uint8_t len) 
{
  transaction_active = true;
  transaction_len = len;
  action(CMD_FT812, ADDR_TRANSACTION_LEN, len);
  action(CMD_FT812, ADDR_ADDRESS_BYTE1, addr); // todo
  action(CMD_FT812, ADDR_ADDRESS_BYTE2, addr);
  action(CMD_FT812, ADDR_ADDRESS_BYTE3, addr);
  action(CMD_FT812, ADDR_ADDRESS_BYTE4, 0); // dummy byte
  // todo: transaction_active = false on on_data()
}

void FT812::setData(uint8_t pos, uint8_t val)
{
  // todo set buf data at pos
  if (pos == transaction_len-1) {
    transaction_active = false;
  }
}

// vim:cin:ai:sts=2 sw=2 ft=cpp
