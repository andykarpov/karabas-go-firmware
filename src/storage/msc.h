#pragma once

void msc_init();
void msc_check_boot();    // msc mode if msc_reboot_to_msc() asked (the sd card is not exposed yet)
void msc_start();         // msc mode: expose the sd card (after the boot core shows the msc screen)
void msc_reboot_to_msc(); // reboot and expose the sd card
bool msc_active();
bool msc_tick();
