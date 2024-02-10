/*
 Copyright (C) 2021 Andy Karpov <andy.karpov@gmail.com>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */

#ifndef __FT812_H__
#define __FT812_H__

#include <Arduino.h>

// Library headers
// Project headers

/****************************************************************************/


/**
 * Driver library for OSD overlay on FPGA sideA
 */

class FT812
{

  using m_cb = void (*)(uint8_t cmd, uint8_t addr, uint8_t data); // alias function pointer

private:
  uint8_t transaction_len;
  bool transaction_active;
  m_cb action;
  uint8_t buf[256];

protected:

public:

  /**
   * Constructor
   */
  FT812();

  /**
   * Begin operation
   *
   * Sets pins correctly, and prepares SPI bus.
   */
  void begin(m_cb act);

  void exclusive(bool on, bool ft);
  void reset();
  void command(uint8_t cmd1, uint8_t cmd2, uint8_t cmd3);
  void write(uint32_t addr, uint8_t *data, uint8_t len);
  void read(uint32_t addr, uint8_t len);
  void setData(uint8_t pos, uint8_t val);

};

#endif // __FT812_H__
// vim:cin:ai:sts=2 sw=2 ft=cpp
