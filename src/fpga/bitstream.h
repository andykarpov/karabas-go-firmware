#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// send the core file bitstream to the fpga, returns number of bytes sent
uint32_t fpga_send(const char* filename);

#ifdef __cplusplus
}
#endif
