#include "main.h"
#include "file_name.h"
#include "ini_update.h"
#include "apps/app_core_browser.h"
#include "ui/settings_items.h"

#define TIMEOUT_MAX 60
#define SOUNDS 5

// in the order of the SET_* ids
const setting_t settings[SET_COUNT] = {
  { "Debug log",      "Log to the USB serial port",     SET_KIND_BOOL },
  { "DVI only",       "No HDMI audio, next core",       SET_KIND_BOOL },
  { "LED matrix",     "The dot matrix add-on",          SET_KIND_BOOL },
  { "Matrix bright.", "Core: as the core sets it",      SET_KIND_CHOICE },
  { "Matrix effects", "The clock and the audio meters", SET_KIND_PAGE },
  { "Clock hours",    "The color of the hours",         SET_KIND_CHOICE },
  { "Clock minutes",  "The color of the minutes",       SET_KIND_CHOICE },
  { "Clock seconds",  "A dot, a colon or a bar",        SET_KIND_CHOICE },
  { "Seconds color",  "The color of the seconds",       SET_KIND_CHOICE },
  { "Meter style",    "How the audio meters look",      SET_KIND_CHOICE },
  { "Meter colors",   "Zones: green, yellow, red",      SET_KIND_CHOICE },
  { "Meter decay",    "How fast the meters fall",       SET_KIND_CHOICE },
  { "Peak hold",      "A dot holds the peak a second",  SET_KIND_BOOL },
  { "Enabled",        "Start a core on power on",       SET_KIND_BOOL },
  { "Timeout",        "Seconds before the start",       SET_KIND_CHOICE },
  { "Core",           "The core to start",              SET_KIND_CHOICE },
  { "Graphic menu",   "Off: the text menu, next start", SET_KIND_BOOL },
  { "Video mode",     "Space at power on: text menu",   SET_KIND_CHOICE },
  { "3D buttons",     "Raised buttons",                 SET_KIND_BOOL },
  { "Show time",      "The clock at the top",           SET_KIND_BOOL },
  { "Show date",      "The date under the clock",       SET_KIND_BOOL },
  { "Character",      "The Karabas picture",            SET_KIND_BOOL },
  { "Mouse speed",    "How fast the pointer moves",     SET_KIND_CHOICE },
  { "Start sound",    "Played when a core starts",      SET_KIND_CHOICE },
  { "Key click",      "A click on every key",           SET_KIND_BOOL },
  { "Theme",          "The colors of the menu",         SET_KIND_CHOICE },
  { "Background",     "Behind everything",              SET_KIND_COLOR },
  { "Gradient",       "The glow at the top",            SET_KIND_COLOR },
  { "Button",         "The buttons",                    SET_KIND_COLOR },
  { "Active button",  "The chosen button",              SET_KIND_COLOR },
  { "Text",           "The text and the buttons",       SET_KIND_COLOR },
  { "Active text",    "The text of the chosen button",  SET_KIND_COLOR },
  { "Footer text",    "The build, page and autoload",   SET_KIND_COLOR },
};

const uint8_t settings_mx_ids[] = {
  SET_MX_CLOCK_HOURS, SET_MX_CLOCK_MINUTES, SET_MX_CLOCK_SECONDS, SET_MX_CLOCK_SECONDS_COLOR,
  SET_MX_METER_STYLE, SET_MX_METER_COLORS, SET_MX_METER_DECAY, SET_MX_METER_PEAK,
};
const uint8_t settings_mx_len = sizeof(settings_mx_ids);

static bool is_mx_opt(uint8_t id) { return id >= SET_MX_CLOCK_HOURS && id < SET_MX_CLOCK_HOURS + KG_MX_OPT_COUNT; }

static const char *const sound_names[SOUNDS] = { "Off", "Klyuchik", "Ta-dan", "Run", "Sound 4" };

typedef struct {
  const char *name;
  uint32_t colors[SET_COLORS]; // bg, gradient, button, active, text, text active, footer
} theme_t;

static const theme_t themes[] = {
  { "Classic",  { 0x010101, 0x0000c8, 0x202026, 0x0000c8, 0xffffff, 0xffffff, 0x787878 } },
  { "Spectrum", { 0x000000, 0x0000d7, 0x10102a, 0xd7d700, 0xffffff, 0x000000, 0x00d7d7 } },
  { "Emerald",  { 0x000400, 0x00702a, 0x102016, 0x00a040, 0xc8ffd0, 0xffffff, 0x4f7a58 } },
  { "Amber",    { 0x0a0500, 0x8a4a00, 0x241808, 0xc87000, 0xffd9a0, 0x1a0d00, 0x8a6a40 } },
  { "Crimson",  { 0x080000, 0xa00018, 0x261012, 0xc8102a, 0xffe0e0, 0xffffff, 0x806060 } },
  { "Ocean",    { 0x000608, 0x006a80, 0x0e2228, 0x00a0b8, 0xe0fcff, 0xffffff, 0x5a8088 } },
  { "Mono",     { 0x000000, 0x505050, 0x202020, 0xc0c0c0, 0xe0e0e0, 0x000000, 0x707070 } },
};
static const uint8_t themes_len = sizeof(themes) / sizeof(themes[0]);

static uint8_t core_sel;  // in cores[], cores_len: the core of karabas.ini that is not on the card
static bool core_other;   // there is such a core
static char core_other_id[sizeof(((setup_t *) 0)->autoload_core)];

// the header fields of the cores are padded with spaces
static void trim_copy(char *out, const char *in, size_t size) {
  while (*in == ' ') in++;
  strlcpy(out, in, size);
  size_t len = strlen(out);
  while (len > 0 && out[len - 1] == ' ') out[--len] = '\0';
}

void settings_begin(setup_t *s) {
  char core_id[sizeof(s->autoload_core)];
  trim_copy(core_id, s->autoload_core, sizeof(core_id));
  strlcpy(s->autoload_core, core_id, sizeof(s->autoload_core));
  if (s->autoload_timeout > TIMEOUT_MAX) s->autoload_timeout = TIMEOUT_MAX;
  if (s->ft_sound >= SOUNDS) s->ft_sound = 1;
  core_sel = cores_len;
  for (uint8_t i = 0; i < cores_len; i++) {
    char id[sizeof(cores[0].id)];
    trim_copy(id, cores[i].id, sizeof(id));
    if (strcmp(id, s->autoload_core) == 0) { core_sel = i; break; }
  }
  core_other = core_sel == cores_len;
  strlcpy(core_other_id, s->autoload_core, sizeof(core_other_id));
  settings_end();
}

void settings_end() {
  matrix_setup(&hw_setup);
  matrix_preview(MATRIX_PREVIEW_NONE);
}

void settings_preview(uint8_t id) {
  if (id >= SET_MX_CLOCK_HOURS && id <= SET_MX_CLOCK_SECONDS_COLOR) matrix_preview(MATRIX_PREVIEW_CLOCK);
  else if (id >= SET_MX_METER_STYLE && id <= SET_MX_METER_PEAK) matrix_preview(MATRIX_PREVIEW_METER);
  else matrix_preview(MATRIX_PREVIEW_NONE);
}

static bool *bool_ref(uint8_t id, setup_t *s) {
  switch (id) {
    case SET_DEBUG: return &s->debug_enabled;
    case SET_DVI: return &s->dvi_only;
    case SET_MATRIX: return &s->matrix_enabled;
    case SET_AUTOLOAD: return &s->autoload_enabled;
    case SET_FT: return &s->ft_enabled;
    case SET_3D: return &s->ft_3d_buttons;
    case SET_TIME: return &s->ft_time;
    case SET_DATE: return &s->ft_date;
    case SET_CHAR: return &s->ft_char;
    case SET_CLICK: return &s->ft_click;
    case SET_MX_METER_PEAK: return &s->matrix_style.meter_peak;
    default: return NULL;
  }
}

bool settings_bool(uint8_t id, const setup_t *s) {
  bool *b = bool_ref(id, (setup_t *) s);
  return b && *b;
}

uint32_t *settings_color(uint8_t id, setup_t *s) {
  switch (id) {
    case SET_COLOR_BG: return &s->color_bg;
    case SET_COLOR_GRADIENT: return &s->color_gradient;
    case SET_COLOR_BUTTON: return &s->color_button;
    case SET_COLOR_ACTIVE: return &s->color_active;
    case SET_COLOR_TEXT: return &s->color_text;
    case SET_COLOR_TEXT_ACTIVE: return &s->color_text_active;
    case SET_COLOR_COPYRIGHT: return &s->color_copyright;
    default: return NULL;
  }
}

void settings_value_text(uint8_t id, const setup_t *s, char *out, size_t size) {
  setup_t *m = (setup_t *) s;
  if (bool_ref(id, m)) {
    strlcpy(out, *bool_ref(id, m) ? "On" : "Off", size);
    return;
  }
  uint32_t *c = settings_color(id, m);
  if (c) {
    snprintf(out, size, "%06lx", (unsigned long) (*c & 0xffffff));
    return;
  }
  if (is_mx_opt(id)) {
    uint8_t opt = id - SET_MX_CLOCK_HOURS;
    strlcpy(out, kg_mx_opt_name(opt, s->matrix_style.opt[opt]), size);
    return;
  }
  switch (id) {
    case SET_TIMEOUT: snprintf(out, size, "%u s", s->autoload_timeout); break;
    case SET_CORE: {
      char name[sizeof(cores[0].name)];
      trim_copy(name, core_sel < cores_len ? cores[core_sel].name : s->autoload_core, sizeof(name));
      kg_short_name(name[0] ? name : "-", out, size - 1);
      break;
    }
    case SET_VIDEO_MODE: strlcpy(out, ft_mode_name(s->ft_video_mode), size); break;
    case SET_MOUSE_SPEED: snprintf(out, size, "%u", s->ft_mouse_speed); break;
    case SET_MATRIX_BRIGHTNESS:
      if (s->matrix_brightness == 0) strlcpy(out, "Core", size);
      else snprintf(out, size, "%u", s->matrix_brightness);
      break;
    case SET_SOUND: strlcpy(out, sound_names[s->ft_sound < SOUNDS ? s->ft_sound : 0], size); break;
    case SET_THEME: {
      int t = settings_theme(s);
      strlcpy(out, t < 0 ? "Custom" : themes[t].name, size);
      break;
    }
    default: out[0] = '\0';
  }
}

void settings_change(uint8_t id, setup_t *s, int d) {
  bool *b = bool_ref(id, s);
  if (b) {
    *b = !*b;
    if (id == SET_MATRIX || id == SET_MX_METER_PEAK) matrix_setup(s); // shown at once
    return;
  }
  if (is_mx_opt(id)) {
    uint8_t opt = id - SET_MX_CLOCK_HOURS, n = kg_mx_opt_count(opt);
    s->matrix_style.opt[opt] = (s->matrix_style.opt[opt] + d + n) % n;
    matrix_setup(s);
    return;
  }
  switch (id) {
    case SET_TIMEOUT: s->autoload_timeout = (s->autoload_timeout + d + TIMEOUT_MAX + 1) % (TIMEOUT_MAX + 1); break;
    case SET_CORE: {
      uint8_t n = cores_len + (core_other ? 1 : 0);
      if (n == 0) break;
      core_sel = (core_sel + d + n) % n;
      if (core_sel < cores_len) trim_copy(s->autoload_core, cores[core_sel].id, sizeof(s->autoload_core));
      else strlcpy(s->autoload_core, core_other_id, sizeof(s->autoload_core));
      break;
    }
    case SET_VIDEO_MODE: {
      uint8_t n = ft_mode_count();
      s->ft_video_mode = (s->ft_video_mode + d + n) % n;
      break;
    }
    case SET_SOUND: s->ft_sound = (s->ft_sound + d + SOUNDS) % SOUNDS; break;
    case SET_MOUSE_SPEED: s->ft_mouse_speed = (s->ft_mouse_speed - 1 + d + 5) % 5 + 1; break;
    case SET_MATRIX_BRIGHTNESS:
      s->matrix_brightness = (s->matrix_brightness + d + 17) % 17;
      matrix_setup(s);
      break;
    case SET_THEME: {
      int t = settings_theme(s);
      if (t < 0) t = d > 0 ? -1 : 0; // Custom: to the first / the last theme
      settings_theme_apply(s, (t + d + themes_len) % themes_len);
      break;
    }
  }
}

bool settings_differ(const setup_t *a, const setup_t *b) {
  for (uint8_t id = 0; id < SET_COUNT; id++) {
    if (id == SET_CORE || id == SET_THEME) continue; // the core below; the theme is the colors
    char va[KG_INI_VALUE_MAX], vb[KG_INI_VALUE_MAX];
    settings_value_text(id, a, va, sizeof(va));
    settings_value_text(id, b, vb, sizeof(vb));
    if (strcmp(va, vb) != 0) return true;
  }
  char ca[sizeof(a->autoload_core)], cb[sizeof(b->autoload_core)];
  trim_copy(ca, a->autoload_core, sizeof(ca));
  trim_copy(cb, b->autoload_core, sizeof(cb));
  return strcmp(ca, cb) != 0;
}

uint8_t settings_theme_count() { return themes_len; }

const char *settings_theme_name(uint8_t i) { return i < themes_len ? themes[i].name : "?"; }

int settings_theme(const setup_t *s) {
  setup_t *m = (setup_t *) s;
  for (uint8_t t = 0; t < themes_len; t++) {
    bool same = true;
    for (uint8_t i = 0; i < SET_COLORS && same; i++) {
      same = (*settings_color(SET_COLOR_BG + i, m) & 0xffffff) == themes[t].colors[i];
    }
    if (same) return t;
  }
  return -1;
}

void settings_theme_apply(setup_t *s, uint8_t t) {
  if (t >= themes_len) return;
  for (uint8_t i = 0; i < SET_COLORS; i++) *settings_color(SET_COLOR_BG + i, s) = themes[t].colors[i];
}
