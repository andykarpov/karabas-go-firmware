#pragma once
// The 24C08 EEPROM next to the RTC (0x50..0x53): 1024 bytes, the upper address bits in the
// device address. Byte reads and writes as the SparkFun library did them: a write only when
// the byte differs, and before every access the chip is polled until its last write cycle
// is over. The caller holds the I2C bus lock. Pure C.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EEPROM24_SIZE 1024

bool eeprom24_present(void);
uint8_t eeprom24_read(uint16_t addr);  // 0xFF on an error
bool eeprom24_write(uint16_t addr, uint8_t val);

#ifdef __cplusplus
}
#endif
