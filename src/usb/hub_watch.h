#pragma once

// The devices behind a USB hub (the FE1.1s of the Mini G, a hub on a port): TinyUSB 0.18 leaves a
// device dead until it is plugged in again when its enumeration fails (3 tries of one transfer),
// stops (a transfer that never ends) or when the hub driver drops its status pipe after a failed
// transfer. hub_watch follows the ports and starts the enumeration again the way the hub driver
// does it (port reset, attach), up to 3 times; a stopped status pipe is armed again after the
// connected ports without a device were looked at. With debug=1 it logs the port events.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void hub_watch_tick(void);            // core1, from the USB loop (RAM, 10 Hz past its check)
void hub_watch_mounted(uint8_t daddr); // tuh_mount_cb: the device of the port being enumerated

// AT+KARABAS=usbhub (APP): the state of the hubs and their ports, and the test requests
void hub_watch_print(void (*out)(const char *line));
uint8_t hub_watch_first_hub(void); // 0: none
bool hub_watch_request_reset(uint8_t hub_addr, uint8_t port); // enumerate the port again
bool hub_watch_request_scan(uint8_t hub_addr);                // look at the ports, as for a stopped pipe
void hub_watch_request_stall(void); // the next hub port enumeration stops at its start (a test)

#ifdef __cplusplus
}
#endif
