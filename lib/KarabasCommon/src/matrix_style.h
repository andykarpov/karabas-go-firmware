#pragma once

// The look of the LED matrix plugins that karabas.ini [matrix] sets for all cores: the clock
// effect and the audio meters. The options have names for the ini (lowercase) and the OSD.
// Pure C++, no Arduino dependencies.

#include <stdint.h>

enum {
  KG_MX_OPT_CLOCK_HOURS,         // the color of the hours
  KG_MX_OPT_CLOCK_MINUTES,       // the color of the minutes
  KG_MX_OPT_CLOCK_SECONDS,       // KG_MX_SEC_*
  KG_MX_OPT_CLOCK_SECONDS_COLOR,
  KG_MX_OPT_METER_STYLE,         // KG_MX_METER_*
  KG_MX_OPT_METER_COLORS,        // KG_MX_METER_COLORS_*
  KG_MX_OPT_METER_DECAY,         // KG_MX_DECAY_*
  KG_MX_OPT_COUNT
};

// the colors of the clock options, in the order of the OSD (not KG_MX_*: see kg_mx_color())
enum { KG_MX_CLR_GREEN, KG_MX_CLR_YELLOW, KG_MX_CLR_RED };

// the seconds: a dot running along the bottom row, a blinking colon, a bar that grows along the
// bottom row in a minute, nothing
enum { KG_MX_SEC_DOT, KG_MX_SEC_COLON, KG_MX_SEC_BAR, KG_MX_SEC_OFF };

// bars: two rows per channel; wide: four rows per channel; center: left to the left of the
// middle, right to the right, all rows; mono: the louder channel on all rows; vertical: two
// columns growing up
enum { KG_MX_METER_BARS, KG_MX_METER_WIDE, KG_MX_METER_CENTER, KG_MX_METER_MONO, KG_MX_METER_VERTICAL };

// zones: green, yellow near the top, red at the top; one color; fire: yellow with a red tip
enum { KG_MX_METER_COLORS_ZONES, KG_MX_METER_COLORS_GREEN, KG_MX_METER_COLORS_YELLOW,
  KG_MX_METER_COLORS_RED, KG_MX_METER_COLORS_FIRE };

enum { KG_MX_DECAY_SLOW, KG_MX_DECAY_NORMAL, KG_MX_DECAY_FAST };

typedef struct {
  uint8_t opt[KG_MX_OPT_COUNT];
  bool meter_peak; // a dot holds the peak for a second, then falls
} kg_mx_style_t;

// the look of old: hours green, minutes yellow, a red dot, two-row bars in zones, normal decay
void kg_mx_style_default(kg_mx_style_t *s);
// the values an option has
uint8_t kg_mx_opt_count(uint8_t opt);
// the name of a value for the OSD ("Green"), "?" out of range
const char *kg_mx_opt_name(uint8_t opt, uint8_t v);
// the key of an option in karabas.ini [matrix] ("clock_hours")
const char *kg_mx_opt_key(uint8_t opt);
// a value of karabas.ini (the name in any case), -1 if unknown
int kg_mx_opt_parse(uint8_t opt, const char *s);
// a KG_MX_CLR_* as a KG_MX_* color
uint8_t kg_mx_color(uint8_t clr);
