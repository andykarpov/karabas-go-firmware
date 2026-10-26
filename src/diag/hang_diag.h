#pragma once

#include <stdint.h>

// A "black box" for hangs (debug=1 only). APP marks where it is (hang_mark), the SPI and I2C
// bus locks note who holds and who waits for them; all of it in the watchdog scratch registers,
// which survive a reset. A watchdog task on core0 feeds the watchdog while APP moves; if APP
// stands still for HANG_APP_MS, or core0 stops altogether, the watchdog resets the board and
// the next boot logs "Last boot hung: ...".
// Scratch 0 is the USB MSC request, 4..7 belong to the SDK (watchdog_reboot).

#define HANG_LOCK_SPI 0
#define HANG_LOCK_I2C 1

void hang_diag_report();   // at boot: logs the last hang, if any, and clears the box
void hang_diag_start();    // with debug=1: the watchdog and its task
void hang_diag_stop();     // before a reboot into BOOTSEL: the bootloader must not be reset
void hang_mark(uint16_t where); // APP only: (tick << 8) | step
void hang_lock_wait(uint8_t lock);
void hang_lock_held(uint8_t lock);
void hang_lock_free(uint8_t lock, bool still_held);
