#pragma once

void msc_init();
void msc_check_boot();    // msc mode if msc_reboot_to_msc() asked (the sd card is not exposed yet)
void msc_start();         // msc mode: expose the sd card (after the boot core shows the msc screen)
void msc_reboot_to_msc(uint8_t sd_card = 1); // reboot and expose the sd card (2: the slot of the FPGA)
bool msc_active();
uint8_t msc_card();       // the card of msc mode: 1, or 2 (the slot of the FPGA)
bool msc_failed();        // no second card: msc_tick() reboots after a while
bool msc_tick();
