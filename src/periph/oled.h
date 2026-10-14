#pragma once

void oled_begin();
void oled_frame();   // one 20 ms frame, from the OLED task
bool oled_present();
