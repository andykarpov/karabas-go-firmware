#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void oled_begin();
void oled_frame();   // one 20 ms frame, from the OLED task
bool oled_present();

#ifdef __cplusplus
}
#endif
