#pragma once
// The PCA9536 4-bit I/O extender of the Karabas Go (0x41): the on-board buttons and LEDs.
// The caller holds the I2C bus lock. Pure C.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool pca9536_present(void);
// bit n set: pin n is an output (the others are inputs with the chip's pull-ups)
bool pca9536_outputs(uint8_t mask);
bool pca9536_write(uint8_t pin, bool high);
// the input, output, polarity and configuration registers (for the log)
bool pca9536_regs(uint8_t regs[4]);
// the levels of the 4 pins (bit n = pin n); false on a read error
bool pca9536_read(uint8_t *levels);

#ifdef __cplusplus
}
#endif
