#pragma once

// The bitmap OSD of MCU link v2 (docs/link_v2.md 5.3): a 256 x 128 monochrome frame buffer drawn
// with u8g2 here and sent to the core by tiles (link_osd2_*). Only the tiles that changed since
// the last osd2_flush() go out. For cores of link v2 with the OSD target (link_osd2()).

#include <stdbool.h>
#include <stdint.h>
#include "u8g2.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OSD2_W 256
#define OSD2_H 128

extern u8g2_t osd2;                // draw with u8g2_* on it, then osd2_flush()

bool osd2_active(void);            // the loaded core has the bitmap OSD
void osd2_begin(void);             // once at start
void osd2_core_loaded(void);       // a core was loaded: the core has an empty bitmap
void osd2_flush(void);             // the changed tiles to the core
void osd2_show(bool on);           // the OSD (the menu)
// a popup window: tiles x, y, w, h (w 0 = none), place 0 centre / 1 top / 2 bottom, zoom
void osd2_popup(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t place, bool zoom);

#ifdef __cplusplus
}
#endif
