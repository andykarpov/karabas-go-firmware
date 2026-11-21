#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// The screen of the boot core in the firmware (state_core_browser). The boot core draws its menu
// itself over RPC (karabas-go-core-menu); the firmware shows only that a boot.kg without RPC is too
// old for it, and how to put a new one on the card.
void app_boot_overlay();
void app_boot_on_keyboard();
#ifdef __cplusplus
}
#endif
