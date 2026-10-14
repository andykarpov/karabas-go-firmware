#pragma once

void oled_begin();
void oled_tick();    // plain build: from the main loop, runs oled_frame() every 20 ms
void oled_frame();   // one 20 ms frame (FreeRTOS: from the OLED task)
bool oled_present();
