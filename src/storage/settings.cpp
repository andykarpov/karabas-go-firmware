#include "main.h"
#include <IniFile.h>
#include "ini_update.h"

// karabas.ini

#define INI_FILE "/karabas.ini"
#define INI_NEW "/karabas.new" // save_setup() writes it, then replaces karabas.ini with it

setup_t hw_setup;

void load_setup() {

  const size_t bufferLen = 80;
  char buffer[bufferLen];
  IniFile ini("karabas.ini");
  
  // defaults
  hw_setup.debug_enabled = false;
  hw_setup.debug_hid = false;
  hw_setup.ft_enabled = true;
  hw_setup.ft_video_mode = 3; // 800x600@60
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
  hw_setup.burst_enabled = true;

  if (!has_sd) return;
  sd1.chvol();

  // a save was cut off between the remove and the rename
  if (!sd1.exists(INI_FILE) && sd1.exists(INI_NEW)) {
    sd1.rename(INI_NEW, INI_FILE);
  }

  if (ini.open()) {
    // validate file (every line, comments included, must fit into the buffer)
    if (!ini.validate(buffer, bufferLen)) {
      // always report it: the defaults are used and debug= is not read either
      hw_setup.debug_enabled = true;
      d_printf("karabas.ini is invalid (error %d, lines are limited to %u chars), using the defaults",
        (int) ini.getError(), (unsigned) (bufferLen - 1)); d_println();
      hw_setup.debug_enabled = false;
      ini.close();
      return;
    }

    ini.getValue("setup", "debug", buffer, bufferLen, hw_setup.debug_enabled);
    ini.getValue("setup", "debug_hid", buffer, bufferLen, hw_setup.debug_hid);
    ini.getValue("setup", "dvi_only", buffer, bufferLen, hw_setup.dvi_only);
    ini.getValue("setup", "burst", buffer, bufferLen, hw_setup.burst_enabled);

    
    ini.getValue("ft812", "enabled", buffer, bufferLen, hw_setup.ft_enabled);
    hw_setup.ft_video_mode = (ini.getValue("ft812", "video_mode", buffer, bufferLen)) ? strtoul(buffer, 0, 10) : 0;
    if (hw_setup.ft_video_mode >= FT812::modeCount()) hw_setup.ft_video_mode = 0;
    hw_setup.ft_sound = (ini.getValue("ft812", "sound", buffer, bufferLen)) ? strtoul(buffer, 0, 10) : 0;
    if (hw_setup.ft_sound > 4) hw_setup.ft_sound = 1;
    ini.getValue("ft812", "click", buffer, bufferLen, hw_setup.ft_click);
    ini.getValue("ft812", "time", buffer, bufferLen, hw_setup.ft_time);
    ini.getValue("ft812", "date", buffer, bufferLen, hw_setup.ft_date);
    ini.getValue("ft812", "char", buffer, bufferLen, hw_setup.ft_char);
    ini.getValue("ft812", "3d_buttons", buffer, bufferLen, hw_setup.ft_3d_buttons);
    if (ini.getValue("ft812", "mouse_speed", buffer, bufferLen)) hw_setup.ft_mouse_speed = strtoul(buffer, 0, 10);
    if (hw_setup.ft_mouse_speed < 1 || hw_setup.ft_mouse_speed > 5) hw_setup.ft_mouse_speed = 3;
    hw_setup.color_bg = (ini.getValue("ft812", "color_bg", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : hw_setup.color_bg;
    hw_setup.color_gradient = (ini.getValue("ft812", "color_gradient", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : hw_setup.color_gradient;
    hw_setup.color_button = (ini.getValue("ft812", "color_button", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : hw_setup.color_button;
    hw_setup.color_active = (ini.getValue("ft812", "color_active", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : hw_setup.color_active;
    hw_setup.color_text = (ini.getValue("ft812", "color_text", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : hw_setup.color_text;
    hw_setup.color_text_active = (ini.getValue("ft812", "color_text_active", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : hw_setup.color_text_active;
    hw_setup.color_copyright = (ini.getValue("ft812", "color_copyright", buffer, bufferLen)) ? strtoul(buffer, 0, 16) : hw_setup.color_copyright;

    ini.getValue("autoload", "enabled", buffer, bufferLen, hw_setup.autoload_enabled);
    hw_setup.autoload_timeout = (ini.getValue("autoload", "timeout", buffer, bufferLen)) ? strtoul(buffer, 0, 10) : 0;
    if (ini.getValue("autoload", "core", buffer, bufferLen)) {
          strcpy(hw_setup.autoload_core, buffer);
    }

    d_println("Setup:");
    d_print("Debug enabled: "); d_println(hw_setup.debug_enabled ? "yes" : "no"); 
    d_print("Debug HID enabled: "); d_println(hw_setup.debug_hid ? "yes" : "no"); 
    d_print("DVI only: "); d_println(hw_setup.dvi_only ? "yes" : "no"); 
    d_print("FT812 enabled: "); d_println(hw_setup.ft_enabled ? "yes" : "no"); 
    d_print("FT812 video mode: "); d_println(hw_setup.ft_video_mode);
    d_print("FT812 sound: "); d_println(hw_setup.ft_sound); 
    d_print("FT812 keypress click enabled: "); d_println(hw_setup.ft_click ? "yes" : "no"); 
    d_print("FT812 show time: "); d_println(hw_setup.ft_time ? "yes" : "no"); 
    d_print("FT812 show date: "); d_println(hw_setup.ft_date ? "yes" : "no"); 
    d_print("FT812 show character: "); d_println(hw_setup.ft_char ? "yes" : "no"); 
    d_print("FT812 use 3d buttons: "); d_println(hw_setup.ft_3d_buttons ? "yes" : "no"); 
    d_print("FT812 mouse speed: "); d_println(hw_setup.ft_mouse_speed);
    d_print("Autoload enabled: "); d_println(hw_setup.autoload_enabled ? "yes" : "no"); 
    d_print("Autoload timeout: "); d_println(hw_setup.autoload_timeout); 
    d_print("Autoload core: "); d_println(hw_setup.autoload_core); 

    ini.close();
    return;
  }
}

typedef struct {
  FsFile *file;
  bool crlf;
  bool ok;
} ini_writer_t;

static void write_line(void *ctx, const char *line) {
  ini_writer_t *w = (ini_writer_t *) ctx;
  size_t len = strlen(line);
  if (w->file->write(line, len) != len) w->ok = false;
  if (w->crlf && w->file->write('\r') != 1) w->ok = false;
  if (w->file->write('\n') != 1) w->ok = false;
}

static void add_item(kg_ini_item_t *items, uint8_t &count, const char *section, const char *key, const char *value) {
  items[count].section = section;
  items[count].key = key;
  strncpy(items[count].value, value, KG_INI_VALUE_MAX);
  items[count].value[KG_INI_VALUE_MAX] = '\0';
  count++;
}

static void add_bool(kg_ini_item_t *items, uint8_t &count, const char *section, const char *key, bool v, bool old) {
  if (v != old) add_item(items, count, section, key, v ? "1" : "0");
}

static void add_uint(kg_ini_item_t *items, uint8_t &count, const char *section, const char *key, unsigned v, unsigned old) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%u", v);
  if (v != old) add_item(items, count, section, key, buf);
}

static void add_color(kg_ini_item_t *items, uint8_t &count, const char *key, uint32_t v, uint32_t old) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%06lx", (unsigned long) (v & 0xffffff));
  if (v != old) add_item(items, count, "ft812", key, buf);
}

bool save_setup(const setup_t &s) {
  kg_ini_item_t items[24];
  uint8_t count = 0;
  const setup_t &o = hw_setup;
  add_bool(items, count, "setup", "debug", s.debug_enabled, o.debug_enabled);
  add_bool(items, count, "setup", "debug_hid", s.debug_hid, o.debug_hid);
  add_bool(items, count, "setup", "burst", s.burst_enabled, o.burst_enabled);
  add_bool(items, count, "setup", "dvi_only", s.dvi_only, o.dvi_only);
  add_bool(items, count, "ft812", "enabled", s.ft_enabled, o.ft_enabled);
  add_uint(items, count, "ft812", "video_mode", s.ft_video_mode, o.ft_video_mode);
  add_uint(items, count, "ft812", "sound", s.ft_sound, o.ft_sound);
  add_bool(items, count, "ft812", "click", s.ft_click, o.ft_click);
  add_bool(items, count, "ft812", "time", s.ft_time, o.ft_time);
  add_bool(items, count, "ft812", "date", s.ft_date, o.ft_date);
  add_bool(items, count, "ft812", "char", s.ft_char, o.ft_char);
  add_color(items, count, "color_bg", s.color_bg, o.color_bg);
  add_color(items, count, "color_gradient", s.color_gradient, o.color_gradient);
  add_color(items, count, "color_button", s.color_button, o.color_button);
  add_color(items, count, "color_active", s.color_active, o.color_active);
  add_color(items, count, "color_text", s.color_text, o.color_text);
  add_color(items, count, "color_text_active", s.color_text_active, o.color_text_active);
  add_color(items, count, "color_copyright", s.color_copyright, o.color_copyright);
  add_bool(items, count, "ft812", "3d_buttons", s.ft_3d_buttons, o.ft_3d_buttons);
  add_uint(items, count, "ft812", "mouse_speed", s.ft_mouse_speed, o.ft_mouse_speed);
  add_bool(items, count, "autoload", "enabled", s.autoload_enabled, o.autoload_enabled);
  add_uint(items, count, "autoload", "timeout", s.autoload_timeout, o.autoload_timeout);
  if (strcmp(s.autoload_core, o.autoload_core) != 0) add_item(items, count, "autoload", "core", s.autoload_core);
  if (count == 0) return true;

  if (!has_sd || msc_active()) return false;
  sd1.chvol();

  FsFile old, f;
  bool has_old = old.open(&sd1, INI_FILE, O_RDONLY);
  if (!f.open(&sd1, INI_NEW, O_WRONLY | O_CREAT | O_TRUNC)) {
    if (has_old) old.close();
    d_println("karabas.ini: can't create " INI_NEW);
    return false;
  }

  ini_writer_t w = { &f, false, true };
  kg_ini_update_t u;
  kg_ini_update_begin(&u, items, count, write_line, &w);
  char line[KG_INI_LINE_MAX + 2]; // the line, its '\n' and the null
  if (has_old) {
    // keep the line ends of the file (fgets drops the CRs)
    int n = old.read(line, sizeof(line) - 1);
    w.crlf = n > 0 && memchr(line, '\r', n) != nullptr;
    old.rewind();
    int len = 0;
    while (w.ok && (len = old.fgets(line, sizeof(line))) > 0) {
      if (line[len - 1] == '\n') {
        line[--len] = '\0';
      } else if (old.available()) {
        d_printf("karabas.ini: a line is longer than %u chars", KG_INI_LINE_MAX); d_println();
        w.ok = false;
        break;
      }
      kg_ini_update_line(&u, line);
    }
    if (len < 0) w.ok = false;
    old.close();
  }
  if (w.ok) kg_ini_update_end(&u);
  if (!f.close()) w.ok = false;

  if (!w.ok) {
    sd1.remove(INI_NEW);
    d_println("karabas.ini: save failed");
    return false;
  }
  if ((has_old && !sd1.remove(INI_FILE)) || !sd1.rename(INI_NEW, INI_FILE)) {
    d_println("karabas.ini: can't replace the file");
    return false;
  }
  hw_setup = s;
  d_printf("karabas.ini: %u values saved", count); d_println();
  return true;
}
