/******************************************************************************
ESP8266ATServer.h
Based on SparkFunESP8266Server.h
ESP8266 WiFi Shield Library Server Header File
Jim Lindblom @ SparkFun Electronics
Original Creation Date: June 20, 2015
http://github.com/sparkfun/SparkFun_ESP8266_AT_Arduino_Library

Development environment specifics:
	IDE: Arduino 1.6.5
	Hardware Platform: Arduino Uno
	ESP8266 WiFi Shield Version: 1.0

This code is beerware; if you see me (or any other SparkFun employee) at the
local, and you've found our code helpful, please buy us a round!

Distributed as-is; no warranty is given.
******************************************************************************/

#ifndef _ESP8266ATSERVER_H_
#define _ESP8266ATSERVER_H_

#include <Arduino.h>
#include <IPAddress.h>
#include "Server.h"
#include "ESP8266AT.h"
#include "ESP8266ATClient.h"

class ESP8266Server : public Server 
{
public:
	ESP8266Server(uint16_t);
	ESP8266Client available(uint8_t wait = 0);
	void begin(ESP8266Class* wifi);
	virtual size_t write(uint8_t);
	virtual size_t write(const uint8_t *buf, size_t size);
	uint8_t status();
	ESP8266Class* esp8266;
	using Print::write;
	
private:
	uint16_t _port;
};

#endif