#include "main.h"
#include "bitmaps.h"
#include "ui/ft_mouse.h"

// GRAM after the images and sounds of the menu: the pointer, then the kept frame
#define POINTER_OFFSET 0xF0000
#define POINTER_W 16
#define POINTER_H 17
#define FRAME_OFFSET (POINTER_OFFSET + 0x400)
#define FRAME_MAX 8192 // the display list RAM
static_assert(KEY4_OFFSET + KEY4_SIZE <= POINTER_OFFSET, "the pointer overlaps the sounds in GRAM");
static_assert(FRAME_OFFSET + FRAME_MAX <= 0x100000, "the kept frame is past the GRAM");

#define HIDE_MS 5000
#define FRAME_MS 15 // pointer frames at most every 15 ms
#define HITS_MAX 40

// X: black, '.': white
static const char *const pointer_art[POINTER_H] = {
  "X               ",
  "XX              ",
  "X.X             ",
  "X..X            ",
  "X...X           ",
  "X....X          ",
  "X.....X         ",
  "X......X        ",
  "X.......X       ",
  "X........X      ",
  "X.....XXXXX     ",
  "X..X..X         ",
  "X.X X..X        ",
  "XX  X..X        ",
  "X    X..X       ",
  "     X..X       ",
  "      XX        ",
};

typedef struct {
  int16_t x, y, w, h, id;
} hit_t;

static hit_t hits[HITS_MAX];
static uint8_t hits_len;

static int16_t px, py;      // the pointer
static int32_t rx, ry;      // the remainders of the speed scaling
static bool visible;
static ElapsedTimer idle_timer;
static ElapsedTimer frame_timer;
static bool frame_pending;  // a move waits for its pointer frame
static uint16_t frame_len;  // the kept frame, 0: none
static uint8_t buttons;     // held

void ft_mouse_load() {
  static uint16_t pix[POINTER_W * POINTER_H];
  for (uint8_t y = 0; y < POINTER_H; y++) {
    for (uint8_t x = 0; x < POINTER_W; x++) {
      char c = pointer_art[y][x];
      pix[y * POINTER_W + x] = c == 'X' ? 0xF000 : c == '.' ? 0xFFFF : 0x0000; // ARGB4
    }
  }
  ft.writeGRAM(POINTER_OFFSET, sizeof(pix), (const uint8_t *) pix, false);
  frame_len = 0;
}

void ft_mouse_reset() {
  hid_mouse_move_t m;
  hid_mouse_take(&m);
  visible = false;
  frame_pending = false;
  rx = ry = 0;
  if (px >= ft.width()) px = ft.width() - 1;
  if (py >= ft.height()) py = ft.height() - 1;
}

void ft_mouse_hide() { visible = false; }

int16_t ft_mouse_x() { return px; }
int16_t ft_mouse_y() { return py; }

// the speed 1..5 of karabas.ini: x0.5, x0.75, x1, x1.5, x2
static int32_t scale(int32_t d, int32_t &rem) {
  static const uint8_t mul[5] = { 2, 3, 4, 6, 8 };
  uint8_t sp = hw_setup.ft_mouse_speed;
  int32_t v = d * mul[(sp >= 1 && sp <= 5 ? sp : 3) - 1] + rem;
  rem = v % 4;
  return v / 4;
}

bool ft_mouse_poll(ft_mouse_event_t *ev) {
  memset(ev, 0, sizeof(*ev));
  hid_mouse_move_t m;
  if (hid_mouse_take(&m)) {
    int32_t dx = scale(m.dx, rx), dy = scale(m.dy, ry);
    bool was_visible = visible;
    if (dx || dy || m.pressed || m.wheel) {
      idle_timer.reset();
      visible = true;
    }
    if (dx || dy) {
      int32_t x = px + dx, y = py + dy;
      px = x < 0 ? 0 : x >= ft.width() ? ft.width() - 1 : x;
      py = y < 0 ? 0 : y >= ft.height() ? ft.height() - 1 : y;
      frame_pending = true;
    }
    if (!was_visible && visible) frame_pending = true;
    buttons = m.buttons;
    ev->pressed = was_visible ? m.pressed : 0;
    ev->wheel = m.wheel > 0 ? 1 : m.wheel < 0 ? -1 : 0;
  }
  if (visible && idle_timer.elapsed() >= HIDE_MS) {
    visible = false;
    frame_pending = true;
  }
  if (frame_pending && frame_timer.elapsed() >= FRAME_MS) {
    frame_pending = false;
    ev->moved = true;
  }
  ev->buttons = buttons;
  return ev->moved || ev->pressed || ev->wheel;
}

void ft_hit_clear() { hits_len = 0; }

bool ft_hit_add(int16_t x, int16_t y, int16_t w, int16_t h, int16_t id) {
  if (hits_len < HITS_MAX) hits[hits_len++] = { x, y, w, h, id };
  return visible && px >= x && px < x + w && py >= y && py < y + h;
}

int16_t ft_hit_at(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < hits_len; i++) {
    const hit_t *h = &hits[i];
    if (x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h) return h->id;
  }
  return FT_HIT_NONE;
}

int16_t ft_hover() { return visible ? ft_hit_at(px, py) : FT_HIT_NONE; }

static void draw_pointer() {
  if (visible) ft.drawBitmapARGB4(POINTER_OFFSET, px, py, POINTER_W, POINTER_H);
  frame_timer.reset();
}

void ft_frame_end() {
  uint16_t len = ft.displayListSize();
  static uint16_t len_max = 0;
  if (len > len_max) { // how close a screen gets to the 8 KB of the display list
    len_max = len;
    d_printf("FT display list: %u bytes (most so far)", len); d_println();
  }
  if (len > 0 && len <= FRAME_MAX) {
    ft.cmdMemcpy(FRAME_OFFSET, FT81x_RAM_DL, len);
    frame_len = len;
  } else {
    frame_len = 0;
  }
  draw_pointer();
  ft.swapScreen();
}

void ft_pointer_frame() {
  if (!frame_len) return;
  ft.beginDisplayList();
  ft.cmdAppend(FRAME_OFFSET, frame_len);
  draw_pointer();
  ft.swapScreen();
}
