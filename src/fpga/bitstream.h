#pragma once

#include <Arduino.h>

// send the core file bitstream to the fpga, returns number of bytes sent
uint32_t fpga_send(const char* filename);
