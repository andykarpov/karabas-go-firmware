#include "main.h"
#include "ini_read.h"
#include "ini_update.h"
#include "time_zone.h"
#include "matrix_style.h"

// karabas.ini

#define INI_FILE "/karabas.ini"
#define INI_NEW "/karabas.new" // save_setup() writes it, then replaces karabas.ini with it
#define INI_MAX 16384 // the most of karabas.ini that is read

// karabas.ini as read by load_setup()
static const char *ini_text;
static size_t ini_len;

static bool ini_get(const char *section, const char *key, char *buf, size_t len) {
  return kg_ini_get(ini_text, ini_len, section, key, buf, len);
}

// the value only when it is one of the bool words (the default stays otherwise)
static void ini_bool(const char *section, const char *key, bool *val) {
  bool v;
  if (kg_ini_get_bool(ini_text, ini_len, section, key, &v)) (*val) = v;
}

setup_t hw_setup;

void load_setup() {

  const size_t bufferLen = 80;
  char buffer[bufferLen];
  
  // defaults
  hw_setup.debug_enabled = false;
  hw_setup.ft_enabled = true;
  hw_setup.ft_video_mode = 1; // 800x600@60
  hw_setup.ft_sound = 1;
  hw_setup.ft_click = true;
  hw_setup.ft_time = true;
  hw_setup.ft_date = true;
  hw_setup.ft_char = true;
  hw_setup.ft_3d_buttons = false;
  hw_setup.ft_mouse_speed = 3;

  hw_setup.autoload_enabled = false;
  hw_setup.autoload_timeout = 15;

  hw_setup.color_bg = 0x00010101;
  hw_setup.color_gradient = 0x000000c8;
  hw_setup.color_button = 0x00202026;
  hw_setup.color_active = 0x000000c8;
  hw_setup.color_text = 0x00ffffff;
  hw_setup.color_text_active = 0x00ffffff;
  hw_setup.color_copyright = 0x00787878;  

  hw_setup.dvi_only = false;
  hw_setup.tz = 0;

  hw_setup.matrix_enabled = true;
  hw_setup.matrix_brightness = 0;
  kg_mx_style_default(&hw_setup.matrix_style);

  if (!has_sd) return;

  // a save was cut off between the remove and the rename
  if (!sd_exists(INI_FILE) && sd_exists(INI_NEW)) {
    f_rename(INI_NEW, INI_FILE);
  }

  size_t len;
  char *text = sd_read_all(INI_FILE, INI_MAX, &len);
  if (text) {
    // validate file (every line, comments included, must fit into the buffer)
    if (!kg_ini_valid(text, len, bufferLen)) {
      // always report it: the defaults are used and debug= is not read either
      hw_setup.debug_enabled = true;
      d_printf("karabas.ini is invalid (lines are limited to %u chars), using the defaults",
        (unsigned) (bufferLen - 2)); d_println();
      hw_setup.debug_enabled = false;
      free(text);
      return;
    }
    ini_text = text;
    ini_len = len;

    ini_bool("setup", "debug", &hw_setup.debug_enabled);
    ini_bool("setup", "dvi_only", &hw_setup.dvi_only);
    int tz;
    if (ini_get("setup", "tz", buffer, bufferLen) && kg_tz_parse(buffer, &tz)) hw_setup.tz = tz;

    
    ini_bool("ft812", "enabled", &hw_setup.ft_enabled);
    if (ini_get("ft812", "video_mode", buffer, bufferLen)) {
      hw_setup.ft_video_mode = strtoul(buffer, 0, 10);
      // 0 640x480@60, 1 800x600@60; the other modes of older firmware are gone
      if (hw_setup.ft_video_mode >= ft_mode_count()) hw_setup.ft_video_mode = 0;
    }
    hw_setup.ft_sound = (ini_get("ft812", "sound", buffer, bufferLen)) ? strtoul(buffer, 0, 10) : 0;
    if (hw_setup.ft_sound > 4) hw_setup.ft_sound = 1;
    ini_bool("ft812", "click", &hw_setup.ft_click);
    ini_bool("ft812", "time", &hw_setup.ft_time);
    ini_bool("ft812", "date", &hw_setup.ft_date);
    ini_bool("ft812", "char", &hw_setup.ft_char);
    ini_bool("ft812", "3d_buttons", &hw_setup.ft_3d_buttons);
    if (ini_get("ft812", "mouse_speed", buffer, bufferLen)) hw_setup.ft_mouse_speed = strtoul(buffer, 0, 10);
    if (hw_setup.ft_mouse_speed < 1 || hw_setup.ft_mouse_speed > 5) hw_setup.ft_mouse_speed = 3;
    hw_setup.color_bg = (ini_get("ft812", "color_bg", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : hw_setup.color_bg;
    hw_setup.color_gradient = (ini_get("ft812", "color_gradient", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : hw_setup.color_gradient;
    hw_setup.color_button = (ini_get("ft812", "color_button", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : hw_setup.color_button;
    hw_setup.color_active = (ini_get("ft812", "color_active", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : hw_setup.color_active;
    hw_setup.color_text = (ini_get("ft812", "color_text", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : hw_setup.color_text;
    hw_setup.color_text_active = (ini_get("ft812", "color_text_active", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : hw_setup.color_text_active;
    hw_setup.color_copyright = (ini_get("ft812", "color_copyright", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : hw_setup.color_copyright;

    ini_bool("matrix", "enabled", &hw_setup.matrix_enabled);
    if (ini_get("matrix", "brightness", buffer, bufferLen)) hw_setup.matrix_brightness = strtoul(buffer, 0, 10);
    if (hw_setup.matrix_brightness > 16) hw_setup.matrix_brightness = 0;
    for (uint8_t i = 0; i < KG_MX_OPT_COUNT; i++) {
      if (!ini_get("matrix", kg_mx_opt_key(i), buffer, bufferLen)) continue;
      int v = kg_mx_opt_parse(i, buffer);
      if (v >= 0) hw_setup.matrix_style.opt[i] = v;
    }
    ini_bool("matrix", "meter_peak", &hw_setup.matrix_style.meter_peak);

    ini_bool("autoload", "enabled", &hw_setup.autoload_enabled);
    hw_setup.autoload_timeout = (ini_get("autoload", "timeout", buffer, bufferLen)) ? strtoul(buffer, 0, 10) : 0;
    if (ini_get("autoload", "core", buffer, bufferLen)) {
          strcpy(hw_setup.autoload_core, buffer);
    }

    d_println("Setup:");
    d_print("Debug enabled: "); d_println(hw_setup.debug_enabled ? "yes" : "no"); 
    d_print("DVI only: "); d_println(hw_setup.dvi_only ? "yes" : "no"); 
    d_printf("Time zone (minutes): %d", hw_setup.tz); d_println();
    d_print("FT812 enabled: "); d_println(hw_setup.ft_enabled ? "yes" : "no"); 
    d_printf("FT812 video mode: %u", hw_setup.ft_video_mode); d_println();
    d_printf("FT812 sound: %u", hw_setup.ft_sound); d_println();
    d_print("FT812 keypress click enabled: "); d_println(hw_setup.ft_click ? "yes" : "no"); 
    d_print("FT812 show time: "); d_println(hw_setup.ft_time ? "yes" : "no"); 
    d_print("FT812 show date: "); d_println(hw_setup.ft_date ? "yes" : "no"); 
    d_print("FT812 show character: "); d_println(hw_setup.ft_char ? "yes" : "no"); 
    d_print("FT812 use 3d buttons: "); d_println(hw_setup.ft_3d_buttons ? "yes" : "no"); 
    d_printf("FT812 mouse speed: %u", hw_setup.ft_mouse_speed); d_println();
    d_print("Matrix enabled: "); d_println(hw_setup.matrix_enabled ? "yes" : "no");
    d_printf("Matrix brightness: %u", hw_setup.matrix_brightness); d_println();
    d_print("Matrix style:");
    for (uint8_t i = 0; i < KG_MX_OPT_COUNT; i++) { d_print(" "); d_print(kg_mx_opt_name(i, hw_setup.matrix_style.opt[i])); }
    d_println(hw_setup.matrix_style.meter_peak ? " peak" : "");
    d_print("Autoload enabled: "); d_println(hw_setup.autoload_enabled ? "yes" : "no"); 
    d_printf("Autoload timeout: %u", hw_setup.autoload_timeout); d_println();
    d_print("Autoload core: "); d_println(hw_setup.autoload_core); 

    free(text);
    ini_text = nullptr;
    return;
  }
}

typedef struct {
  FIL *file;
  bool crlf;
  bool ok;
} ini_writer_t;

static void write_line(void *ctx, const char *line) {
  ini_writer_t *w = (ini_writer_t *) ctx;
  size_t len = strlen(line);
  if (sd_write(w->file, line, len) != len) w->ok = false;
  if (w->crlf && sd_write(w->file, "\r", 1) != 1) w->ok = false;
  if (sd_write(w->file, "\n", 1) != 1) w->ok = false;
}

static void add_item(kg_ini_item_t *items, uint8_t *count, const char *section, const char *key, const char *value) {
  items[(*count)].section = section;
  items[(*count)].key = key;
  strncpy(items[(*count)].value, value, KG_INI_VALUE_MAX);
  items[(*count)].value[KG_INI_VALUE_MAX] = '\0';
  (*count)++;
}

static void add_bool(kg_ini_item_t *items, uint8_t *count, const char *section, const char *key, bool v, bool old) {
  if (v != old) add_item(items, count, section, key, v ? "1" : "0");
}

static void add_uint(kg_ini_item_t *items, uint8_t *count, const char *section, const char *key, unsigned v, unsigned old) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%u", v);
  if (v != old) add_item(items, count, section, key, buf);
}

static void add_color(kg_ini_item_t *items, uint8_t *count, const char *key, uint32_t v, uint32_t old) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%06lx", (unsigned long) (v & 0xffffff));
  if (v != old) add_item(items, count, "ft812", key, buf);
}

bool save_setup(const setup_t *s) {
  kg_ini_item_t items[29 + KG_MX_OPT_COUNT + 1];
  uint8_t count = 0;
  const setup_t *o = &hw_setup;
  add_bool(items, &count, "setup", "debug", s->debug_enabled, o->debug_enabled);
  add_bool(items, &count, "setup", "dvi_only", s->dvi_only, o->dvi_only);
  if (s->tz != o->tz) {
    char buf[8];
    kg_tz_format(s->tz, buf, sizeof(buf));
    add_item(items, &count, "setup", "tz", buf);
  }
  add_bool(items, &count, "ft812", "enabled", s->ft_enabled, o->ft_enabled);
  add_uint(items, &count, "ft812", "video_mode", s->ft_video_mode, o->ft_video_mode);
  add_uint(items, &count, "ft812", "sound", s->ft_sound, o->ft_sound);
  add_bool(items, &count, "ft812", "click", s->ft_click, o->ft_click);
  add_bool(items, &count, "ft812", "time", s->ft_time, o->ft_time);
  add_bool(items, &count, "ft812", "date", s->ft_date, o->ft_date);
  add_bool(items, &count, "ft812", "char", s->ft_char, o->ft_char);
  add_color(items, &count, "color_bg", s->color_bg, o->color_bg);
  add_color(items, &count, "color_gradient", s->color_gradient, o->color_gradient);
  add_color(items, &count, "color_button", s->color_button, o->color_button);
  add_color(items, &count, "color_active", s->color_active, o->color_active);
  add_color(items, &count, "color_text", s->color_text, o->color_text);
  add_color(items, &count, "color_text_active", s->color_text_active, o->color_text_active);
  add_color(items, &count, "color_copyright", s->color_copyright, o->color_copyright);
  add_bool(items, &count, "ft812", "3d_buttons", s->ft_3d_buttons, o->ft_3d_buttons);
  add_uint(items, &count, "ft812", "mouse_speed", s->ft_mouse_speed, o->ft_mouse_speed);
  add_bool(items, &count, "matrix", "enabled", s->matrix_enabled, o->matrix_enabled);
  add_uint(items, &count, "matrix", "brightness", s->matrix_brightness, o->matrix_brightness);
  for (uint8_t i = 0; i < KG_MX_OPT_COUNT; i++) {
    if (s->matrix_style.opt[i] == o->matrix_style.opt[i]) continue;
    char name[16];
    strlcpy(name, kg_mx_opt_name(i, s->matrix_style.opt[i]), sizeof(name));
    for (char *c = name; *c; c++) *c = tolower(*c);
    add_item(items, &count, "matrix", kg_mx_opt_key(i), name);
  }
  add_bool(items, &count, "matrix", "meter_peak", s->matrix_style.meter_peak, o->matrix_style.meter_peak);
  add_bool(items, &count, "autoload", "enabled", s->autoload_enabled, o->autoload_enabled);
  add_uint(items, &count, "autoload", "timeout", s->autoload_timeout, o->autoload_timeout);
  if (strcmp(s->autoload_core, o->autoload_core) != 0) add_item(items, &count, "autoload", "core", s->autoload_core);
  if (count == 0) return true;

  if (!has_sd || msc_active()) return false;

  size_t old_len = 0;
  char *old = sd_read_all(INI_FILE, INI_MAX, &old_len);
  bool has_old = old != nullptr;
  static FIL f; // not on the stack: a FIL has its own sector buffer
  if (!sd_open(&f, INI_NEW, FA_WRITE | FA_CREATE_ALWAYS)) {
    free(old);
    d_println("karabas.ini: can't create " INI_NEW);
    return false;
  }

  // keep the line ends of the file
  ini_writer_t w = { &f, has_old && memchr(old, '\r', old_len) != nullptr, true };
  kg_ini_update_t u;
  kg_ini_update_begin(&u, items, count, write_line, &w);
  for (size_t pos = 0; has_old && w.ok && pos < old_len; ) {
    char *line = old + pos;
    char *nl = (char *) memchr(line, '\n', old_len - pos);
    size_t len = nl ? (size_t) (nl - line) : old_len - pos;
    pos += len + (nl ? 1 : 0);
    line[len] = '\0';
    // the CRs go (the line ends of the file are written back by write_line)
    size_t k = 0;
    for (size_t i = 0; i < len; i++) if (line[i] != '\r') line[k++] = line[i];
    line[k] = '\0';
    if (k > KG_INI_LINE_MAX) {
      d_printf("karabas.ini: a line is longer than %u chars", KG_INI_LINE_MAX); d_println();
      w.ok = false;
      break;
    }
    kg_ini_update_line(&u, line);
  }
  free(old);
  if (w.ok) kg_ini_update_end(&u);
  if (f_close(&f) != FR_OK) w.ok = false;

  if (!w.ok) {
    f_unlink(INI_NEW);
    d_println("karabas.ini: save failed");
    return false;
  }
  if ((has_old && f_unlink(INI_FILE) != FR_OK) || f_rename(INI_NEW, INI_FILE) != FR_OK) {
    d_println("karabas.ini: can't replace the file");
    return false;
  }
  hw_setup = *s;
  matrix_setup(&hw_setup);
  d_printf("karabas.ini: %u values saved", count); d_println();
  return true;
}
