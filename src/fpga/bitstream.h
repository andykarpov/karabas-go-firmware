#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// send the core file bitstream to the fpga, returns number of bytes sent
uint32_t fpga_send(const char* filename);

// the buffer of fpga_send(), free after it: read_roms() reads the ROMs into it
#define BITSTREAM_BUF_SIZE 8192
uint8_t *bitstream_buf(void);

#ifdef __cplusplus
}
#endif
