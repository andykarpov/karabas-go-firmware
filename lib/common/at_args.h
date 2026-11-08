#pragma once

// The arguments of the AT+KARABAS commands that drive the mouse, the pads and the file items
// (periph/at_control).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KG_AT_MOUSE_MAX 4000 // the longest move of one command, each axis
#define KG_AT_MOUSE_KEEP (-1)

typedef struct {
  int dx, dy, wheel;
  int buttons; // the mask (bit 0 left, 1 right, 2 middle), KG_AT_MOUSE_KEEP: the ones held stay
} kg_at_mouse_t;

// "<dx>,<dy>[,<buttons>[,<wheel>]]"; buttons may be empty ("5,0,,1"). false: not numbers, or out
// of range
bool kg_at_mouse_parse(const char *s, kg_at_mouse_t *m);
// the reports a move needs, at least 1: a report carries -127..127 on each axis
int kg_at_mouse_steps(int dx, int dy);
// the part of *rest for one report; *rest is what remains
int8_t kg_at_mouse_step(int *rest);
// left, right, middle in any case; "" is left
bool kg_at_mouse_button(const char *name, uint8_t *mask);

// Sega button bits of "up+a": up down left right start a b c x y z mode, in any case, joined by
// '+'. false: an unknown name or nothing
bool kg_at_pad_parse(const char *s, uint16_t *buttons);

// "/games/x.trd" -> "/games" and "x.trd", "x.trd" and "/x.trd" -> "/" and "x.trd". false: no file
// name, or a part does not fit
bool kg_at_path_split(const char *path, char *dir, size_t dir_size, char *name, size_t name_size);

#ifdef __cplusplus
}
#endif
