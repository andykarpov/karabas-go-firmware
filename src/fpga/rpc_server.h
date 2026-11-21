// The services of the firmware for the calls of a core over link v2 RPC (target 08,
// docs/link_v2.md 5.9): SYS, CFG, CORES (the self-drawing boot menu, karabas-go-core-menu docs/PLAN.md).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rpc_server_init(void);   // once at start: takes the requests of link_v2_service
void rpc_server_tick(void);   // the main loop: what must not run inside the link service (a core start)
bool rpc_boot_menu(void);     // the boot core draws the menu itself (it has RPC): no menu of the firmware

#ifdef __cplusplus
}
#endif
