#pragma once

void msc_init();
void msc_check_boot();   // expose the sd card if btn2 is held on boot or msc_reboot_to_msc() asked
void msc_reboot_to_msc(); // reboot and expose the sd card
bool msc_active();
bool msc_tick(bool btn1);
