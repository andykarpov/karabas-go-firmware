#pragma once

#include <Arduino.h>
#include "types.h"

// The karabas.ini settings that the OSD Settings screens edit (BOOT core only): their names, hints,
// values and how they change. The classic screen (app_settings) and the graphic one (app_ft_setup)
// draw them their own way; both edit a copy of hw_setup and save it with save_setup().

enum {
  SET_DEBUG, SET_DVI, SET_MATRIX, SET_MATRIX_BRIGHTNESS,
  SET_AUTOLOAD, SET_TIMEOUT, SET_CORE,
  SET_FT, SET_VIDEO_MODE, SET_3D, SET_TIME, SET_DATE, SET_CHAR, SET_MOUSE_SPEED,
  SET_SOUND, SET_CLICK,
  SET_THEME,
  SET_COLOR_BG, SET_COLOR_GRADIENT, SET_COLOR_BUTTON, SET_COLOR_ACTIVE, SET_COLOR_TEXT,
  SET_COLOR_TEXT_ACTIVE, SET_COLOR_COPYRIGHT,
  SET_COUNT
};

#define SET_COLORS (SET_COLOR_COPYRIGHT - SET_COLOR_BG + 1)

enum { SET_KIND_BOOL, SET_KIND_CHOICE, SET_KIND_COLOR };

typedef struct {
  const char *label;
  const char *hint; // at most 30 chars, the width of the classic OSD
  uint8_t kind;
} setting_t;

extern const setting_t settings[SET_COUNT];

// starts editing s (a copy of hw_setup): picks the autoload core in the core list
void settings_begin(setup_t &s);
// editing is over (saved or not): what shows at once while editing (the LED matrix) goes back
// to hw_setup
void settings_end();
// the value as text ("On", "5 s", "800x600@60", "0000c8")
void settings_value_text(uint8_t id, const setup_t &s, char *out, size_t size);
bool settings_bool(uint8_t id, const setup_t &s);
// the next (d = 1) / previous (d = -1) value; a color does not change this way
void settings_change(uint8_t id, setup_t &s, int d);
uint32_t *settings_color(uint8_t id, setup_t &s);
// a setting of the screens differs (the unsaved changes)
bool settings_differ(const setup_t &a, const setup_t &b);

// the color themes of the graphic menu; settings_theme() is -1 for colors of no theme (Custom)
uint8_t settings_theme_count();
const char *settings_theme_name(uint8_t i);
int settings_theme(const setup_t &s);
void settings_theme_apply(setup_t &s, uint8_t i);
