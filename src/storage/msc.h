#pragma once

void msc_init();
void msc_check_boot();   // expose the sd card if btn2 is held on boot
bool msc_active();
bool msc_tick(bool btn1);
