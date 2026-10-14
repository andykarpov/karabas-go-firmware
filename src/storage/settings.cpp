#include "main.h"
#include <IniFile.h>

// karabas.ini

setup_t hw_setup;

void load_setup() {

  const size_t bufferLen = 80;
  char buffer[bufferLen];
  IniFile ini("karabas.ini");
  
  // defaults
  hw_setup.debug_enabled = false;
  hw_setup.debug_hid = false;
  hw_setup.ft_enabled = true;
  hw_setup.ft_video_mode = 0;
  hw_setup.ft_sound = 1;
  hw_setup.ft_click = true;
  hw_setup.ft_time = true;
  hw_setup.ft_date = true;
  hw_setup.ft_char = true;
  hw_setup.ft_3d_buttons = false;

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
    if (hw_setup.ft_video_mode > 15) hw_setup.ft_video_mode = 0;
    hw_setup.ft_sound = (ini.getValue("ft812", "sound", buffer, bufferLen)) ? strtoul(buffer, 0, 10) : 0;
    if (hw_setup.ft_sound > 4) hw_setup.ft_sound = 1;
    ini.getValue("ft812", "click", buffer, bufferLen, hw_setup.ft_click);
    ini.getValue("ft812", "time", buffer, bufferLen, hw_setup.ft_time);
    ini.getValue("ft812", "date", buffer, bufferLen, hw_setup.ft_date);
    ini.getValue("ft812", "char", buffer, bufferLen, hw_setup.ft_char);
    ini.getValue("ft812", "3d_buttons", buffer, bufferLen, hw_setup.ft_3d_buttons);
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
    d_print("Autoload enabled: "); d_println(hw_setup.autoload_enabled ? "yes" : "no"); 
    d_print("Autoload timeout: "); d_println(hw_setup.autoload_timeout); 
    d_print("Autoload core: "); d_println(hw_setup.autoload_core); 

    ini.close();
    return;
  }
}
