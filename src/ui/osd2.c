#include "main.h"
#include "ui/osd2.h"

u8g2_t osd2;

static uint8_t buf[OSD2_W * OSD2_H / 8];   // the frame buffer of u8g2 (page layout)
static uint8_t sent[OSD2_W * OSD2_H / 8];  // what the core has
static bool started = false;

static const u8x8_display_info_t info = {
  .tile_width = OSD2_W / 8, .tile_height = OSD2_H / 8, .pixel_width = OSD2_W, .pixel_height = OSD2_H,
};

// u8g2 calls these only for a real display: the buffer goes out in osd2_flush()
static uint8_t display_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  (void) arg_int; (void) arg_ptr;
  if (msg == U8X8_MSG_DISPLAY_SETUP_MEMORY) u8x8_d_helper_display_setup_memory(u8x8, &info);
  return 1;
}

static uint8_t dummy_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  (void) u8x8; (void) msg; (void) arg_int; (void) arg_ptr;
  return 1;
}

void osd2_begin(void) {
  if (started) return;
  u8g2_SetupDisplay(&osd2, display_cb, u8x8_cad_empty, dummy_cb, dummy_cb);
  u8g2_SetupBuffer(&osd2, buf, OSD2_H / 8, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
  u8x8_InitDisplay(u8g2_GetU8x8(&osd2));
  u8g2_ClearBuffer(&osd2);
  started = true;
}

bool osd2_active(void) {
  return started && link_osd2();
}

void osd2_core_loaded(void) {
  osd2_begin();
  memset(sent, 0, sizeof(sent)); // the bitmap of a core starts at 0
}

void osd2_flush(void) {
  if (!osd2_active()) return;
  // runs of changed tiles (8 bytes each), one write per run
  const uint16_t tiles = sizeof(buf) / 8;
  uint16_t t = 0;
  while (t < tiles) {
    if (memcmp(buf + t * 8, sent + t * 8, 8) == 0) { t++; continue; }
    uint16_t start = t;
    while (t < tiles && memcmp(buf + t * 8, sent + t * 8, 8) != 0) t++;
    link_osd2_write(start, buf + start * 8, (t - start) * 8);
    memcpy(sent + start * 8, buf + start * 8, (t - start) * 8);
  }
}

void osd2_show(bool on) {
  link_osd2_enable(on);
}

void osd2_popup(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t place, bool zoom) {
  link_osd2_popup(x, y, w, h, place, zoom);
}
