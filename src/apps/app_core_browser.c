#include "config.h"
#include "types.h"
#include "main.h"
#include "ft812.h"
#include "ui/bitmaps.h"
#include "osd.h"
#include "input/sega.h"
#include "input/usb_hid_keys.h"
#include "apps/app_core_browser.h"
#include "apps/app_setup.h"
#include "ui/sorts.h"
#include "file_name.h"
#include "ui/ft_mouse.h"
#include "ui/ft_page.h"
#include <stdio.h>
#include <stdlib.h>
#include "ui/ui_draw.h"
#include "kg_list.h"

core_list_item_t *cores = NULL; // on the heap, cores_len items
uint8_t cores_len = 0;
#define CORES_CHUNK 16
static uint8_t core_sel = 0;
static const uint8_t core_page_size = MAX_CORES_PER_PAGE;
// the graphic list: as many buttons as fit between the logo and the page line
#define FT_CORE_TOP (64 + 16 + 8)
#define FT_CORE_STEP 40
#define FT_CORE_BOTTOM 60 // the page line and the footer
static uint8_t core_pages = 1;
static uint8_t core_page = 1;

static uint8_t ft_core_page_size() {
  int n = (ft_height() - FT_CORE_BOTTOM - FT_CORE_TOP) / FT_CORE_STEP;
  return n < 1 ? 1 : n;
}

// the cores on a page of the menu on screen (Left / Right jump by it)
static uint8_t page_size() { return has_ft ? ft_core_page_size() : core_page_size; }
static uint32_t autoload_timer = 0;
static bool autoload_enabled;
static uint32_t autoload_countdown;

// the clickable areas of the graphic browser besides the cores (0..cores_len-1)
enum { HIT_SETUP = 1000, HIT_PREV, HIT_NEXT };
static int16_t hover = FT_HIT_NONE; // under the pointer at the last draw
static bool setup_focus = false;    // the graphic browser: the keys are on the Setup button

// the whole seconds left until the autoload
static uint32_t autoload_left_s() {
  uint32_t ms = ms_since(autoload_timer);
  return ms < autoload_countdown * 1000 ? (autoload_countdown * 1000 - ms) / 1000 : 0;
}

void app_core_browser_overlay() {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_clear();
  osd_header(core.build, core.id, HW_ID);
  print_time();
  app_core_browser_menu(APP_COREBROWSER_MENU_OFFSET);
  // footer
  osd_line(UI_ROW_HINT_LINE - 2);
  osd_set_pos(0, UI_ROW_HINT_LINE - 1);
  ui_hotkey("S", "etup");
  osd_set_pos(OSD_SIZE_X - 3, UI_ROW_HINT_LINE - 1); // right of the page number
  ui_hotkey("G", "UI");
  ui_hint("Please use arrows to navigate", "Press Enter to load selection");
  osd_update();
}

void app_core_browser_menu(uint8_t vpos) {
  core_pages = kg_list_pages(cores_len, core_page_size);
  core_page = kg_list_page(core_sel, core_page_size);
  uint8_t core_from = (core_page-1)*core_page_size;
  uint8_t core_to = core_page*core_page_size > cores_len ? cores_len : core_page*core_page_size;
  for (uint8_t row = 0; row < core_page_size; row++) {
    uint8_t i = core_from + row;
    char line[40] = "";
    // number, name (19 chars), build
    if (i < core_to) {
      snprintf(line, sizeof(line), "%-3d %-19.19s %s", i+1, cores[i].name, cores[i].build);
    }
    ui_row(0, vpos + row, line, i == core_sel && i < core_to);
  }
  osd_set_pos(8, vpos + core_page_size + 1); osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  char b[24];
  snprintf(b, sizeof(b), "Page %02d of %02d", core_page, core_pages);
  osd_print(b);
  osd_update();
}

static_assert(KEY1_PCM_START + KEY1_PCM_SIZE <= sizeof(key1Data), "sound 1 is past its array");
static_assert(KEY2_PCM_START + KEY2_PCM_SIZE <= sizeof(key2Data), "sound 2 is past its array");
static_assert(KEY3_PCM_START + KEY3_PCM_SIZE <= sizeof(key3Data), "sound 3 is past its array");
static_assert(KEY4_PCM_START + KEY4_PCM_SIZE <= sizeof(key4Data), "sound 4 is past its array");

void app_core_browser_ft_load() {
  ft_set_sound(FT81x_SOUND_COWBELL, 60);
  ft_load_image(LOGO_OFFSET, LOGO_SIZE, logoData); // karabas logo, starts from 0 in GRAM
  ft_load_image(BG_OFFSET, BG_SIZE, bgData); // karabas bg, starts after logo in GRAM
  ft_write_gram(KEY1_OFFSET, KEY1_PCM_SIZE, key1Data + KEY1_PCM_START); // pcm sound, starts from after logo and bg in GRAM
  ft_write_gram(KEY2_OFFSET, KEY2_PCM_SIZE, key2Data + KEY2_PCM_START); // pcm sound, starts from after logo and bg in GRAM
  ft_write_gram(KEY3_OFFSET, KEY3_PCM_SIZE, key3Data + KEY3_PCM_START); // pcm sound, starts from after logo and bg in GRAM
  ft_write_gram(KEY4_OFFSET, KEY4_PCM_SIZE, key4Data + KEY4_PCM_START); // pcm sound, starts from after logo and bg in GRAM
  ft_mouse_load();
}

void app_core_browser_ft_overlay() {
  app_core_browser_ft_load();
  ft_mouse_reset();
  app_core_browser_ft_menu(0);
}

void app_core_browser_ft_background(const setup_t *t) {
  ft_clear(t->color_bg);
  // from the top to 3/4 of the height; below is the clear color
  ft_draw_gradient_v(0, t->color_gradient, ft_height() - ft_height() / 4 - 1, t->color_bg);
}

void app_core_browser_ft_decor(const setup_t *t, bool show_char) {
  if (t->ft_time) {
    char time[12];
    sprintf(time, "%02d:%02d:%02d", rtc_get_hour(), rtc_get_minute(), rtc_get_second());
    ft_draw_text(ft_width()-88, 30, 30, t->color_text, FT81x_OPT_CENTER, time);
  }
  if (t->ft_date) {
    char date[12];
    sprintf(date, "%02d/%02d/%02d", rtc_get_day(), rtc_get_month(), rtc_get_year() % 100);
    ft_draw_text(ft_width()-88, 60, 30, t->color_text, FT81x_OPT_CENTER, date);
  }
  ft_draw_bitmap(0, 8, 8, LOGO_WIDTH, LOGO_HEIGHT, 4, 0);  // logo scaled 4x
  if (t->ft_char && show_char) {
    ft_draw_bitmap(LOGO_BITMAP_SIZE, ft_width()-BG_WIDTH-8, ft_height()-BG_HEIGHT-8, BG_WIDTH, BG_HEIGHT, 1, 0); // bg image
  }
}

// the start sounds of the graphic menu (hw_setup.ft_sound 1..4), loaded into GRAM
typedef struct { uint32_t offset, size, samplerate, duration; } ft_sound_t;
static const ft_sound_t ft_sounds[] = {
  { KEY1_OFFSET, KEY1_PCM_SIZE, KEY1_SAMPLERATE, KEY1_DURATION },
  { KEY2_OFFSET, KEY2_PCM_SIZE, KEY2_SAMPLERATE, KEY2_DURATION },
  { KEY3_OFFSET, KEY3_PCM_SIZE, KEY3_SAMPLERATE, KEY3_DURATION },
  { KEY4_OFFSET, KEY4_PCM_SIZE, KEY4_SAMPLERATE, KEY4_DURATION },
};

void app_core_browser_ft_sound(uint8_t sound, bool wait) {
  const ft_sound_t *k = &ft_sounds[sound >= 1 && sound <= 4 ? sound - 1 : 0];
  // a length that is not a multiple of 8 plays on past the sound (the last few samples go)
  ft_play_audio(k->offset, k->size & ~7u, k->samplerate, FT81x_AUDIO_FORMAT_ULAW, false);
  if (wait) {
    ms_delay(k->duration);
    ft_stop_audio(); // not stopSound(): that silences the click effect (REG_SOUND) instead
  }
}

void app_core_browser_ft_menu(uint8_t play_sounds) {

  ft_begin_display_list();

  uint32_t color_button = hw_setup.color_button;
  uint32_t color_button_active = hw_setup.color_active;
  uint32_t color_text = hw_setup.color_text;
  uint32_t color_text_active = hw_setup.color_text_active;
  uint32_t color_copyright = hw_setup.color_copyright;

  app_core_browser_ft_background(&hw_setup);
  ft_hit_clear();

  uint8_t ft_page = ft_core_page_size();
  core_pages = kg_list_pages(cores_len, ft_page);
  core_page = kg_list_page(core_sel, ft_page);
  uint8_t core_from = (core_page-1)*ft_page;
  uint8_t core_to = core_page*ft_page > cores_len ? cores_len : core_page*ft_page;
  // the button of a core: the middle half of the screen, a row each
  const int16_t bx = ft_width() / 4 + 8, bw = ft_width() / 2 - 16, bh = 32;
  for (uint8_t i = core_from; i < core_to; i++) {
    const int16_t by = FT_CORE_TOP + (i - core_from) * FT_CORE_STEP;
    char name[18];
    snprintf(name, sizeof(name), "%s", cores[i].name);
    const bool sel = i == core_sel && !setup_focus;
    const uint32_t colorb = sel ? color_button_active : color_button;
    const uint32_t colort = sel ? color_text_active : color_text;
    if (ft_hit_add(bx, by, bw, bh, i)) {
      // hovered: a rect grows by its corner radius, so this is a 2 px frame around the button
      ft_draw_rect(bx, by, bw, bh, 2, color_button_active);
    }
    ft_draw_button(bx, by, bw, bh, 28, colort, colorb, ft_button_opt(hw_setup.ft_3d_buttons), name);
    if (autoload_enabled && i == core_sel) {
      ft_draw_gauge(bx + 20, by + bh / 2, 12, colort, colorb, 0, 1, 1, autoload_left_s(), autoload_countdown);
    }
  }

  char b[40]; 

  sprintf(b, "FW build: %s", STRINGIZE_VALUE_OF(BUILD_VER));
  ft_draw_text(112, ft_height()-40, 27, color_copyright, FT81x_OPT_CENTER, b);
  sprintf(b, "(c) %s andykarpov", __DATE__ + 7); // the year of the build ("Oct 28 2026")
  ft_draw_text(112, ft_height()-20, 27, color_copyright, FT81x_OPT_CENTER, b);
  sprintf(b, "Page %d of %d", core_page, core_pages);
  ft_draw_text(ft_width()/2, ft_height()-40, 27, color_copyright, FT81x_OPT_CENTER, b);
  if (core_pages > 1) {
    bool h = ft_hit_add(ft_width()/2 - 96, ft_height()-54, 32, 28, HIT_PREV);
    ft_draw_text(ft_width()/2 - 80, ft_height()-40, 28, h ? color_text : color_copyright, FT81x_OPT_CENTER, "<");
    h = ft_hit_add(ft_width()/2 + 64, ft_height()-54, 32, 28, HIT_NEXT);
    ft_draw_text(ft_width()/2 + 80, ft_height()-40, 28, h ? color_text : color_copyright, FT81x_OPT_CENTER, ">");
  }

  if (autoload_enabled) sprintf(b, "Autoloading core in %lu s", (unsigned long) autoload_left_s());
  else sprintf(b, "Autoload disabled");
  ft_draw_text(ft_width()/2, ft_height()-20, 27, color_copyright, FT81x_OPT_CENTER, b);

  if (play_sounds == 1 && hw_setup.ft_click) {
      ft_play_sound();
  } else if (play_sounds == 2 && hw_setup.ft_sound > 0) {
    app_core_browser_ft_sound(hw_setup.ft_sound, true);
  }

  // the Setup button (S) under the logo
  if (ft_hit_add(14, 82, 120, 44, HIT_SETUP)) ft_draw_circle(36, 104, 20, color_button_active);
  ft_draw_circle(36, 104, 18, setup_focus ? color_button_active : color_button);
  ft_draw_text(36, 104, 29, setup_focus ? color_text_active : color_text, FT81x_OPT_CENTER, "S");
  ft_draw_text(62, 104, 28, color_text, FT81x_OPT_CENTERY, "Setup");

  app_core_browser_ft_decor(&hw_setup, true);

  hover = ft_hover();
  ft_frame_end();
}


// the header of the open core file (file1)
core_list_item_t app_core_browser_get_item(const char *filename) {
  core_list_item_t core;
  snprintf(core.filename, sizeof(core.filename), "%s", filename);
  file_read_str(&file1, FILE_POS_CORE_ID, core.id, 32);
  file_read_str(&file1, FILE_POS_CORE_NAME, core.name, 32);
  uint8_t visible; file_seek(FILE_POS_CORE_VISIBLE); visible = file_read(); core.visible = (visible > 0);
  file_seek(FILE_POS_CORE_ORDER); core.order = file_read();
  file_seek(FILE_POS_CORE_TYPE); core.type = file_read();
  file_read_str(&file1, FILE_POS_CORE_BUILD, core.build, 8);
  return core;
}

// the next free item of cores[], NULL at MAX_CORES or out of memory
static uint8_t cores_cap = 0;
static core_list_item_t *add_core(void) {
  if (cores_len >= MAX_CORES) {
    d_printf("Core list: more than %d cores, the rest is left out", MAX_CORES); d_println();
    return NULL;
  }
  if (cores_len >= cores_cap) {
    uint16_t c = cores_cap + CORES_CHUNK;
    if (c > MAX_CORES) c = MAX_CORES;
    core_list_item_t *p = (core_list_item_t *) realloc(cores, c * sizeof(core_list_item_t));
    if (p == NULL) {
      d_printf("Core list: out of memory at %d cores", cores_len); d_println();
      return NULL;
    }
    cores = p;
    cores_cap = (uint8_t) c;
  }
  return &cores[cores_len++];
}

void app_core_browser_read_list() {
  free(cores);
  cores = NULL;
  cores_len = 0;
  cores_cap = 0;

  // files from sd card
  if (has_sd) {
    if (!sd_opendir(&root1, "/")) {
      return;
    }
    FILINFO fi;
    while (f_readdir(&root1, &fi) == FR_OK && fi.fname[0]) {
      if (!(fi.fattrib & AM_DIR) && strlen(fi.fname) < 32 && kg_name_has_ext(fi.fname, CORE_EXT) &&
          sd_open(&file1, fi.fname, FA_READ)) {
        core_list_item_t *item = add_core();
        if (item) *item = app_core_browser_get_item(fi.fname);
        sd_close(&file1);
        if (!item) break;
      }
    }
    // give the unused end of the last chunk back
    if (cores_len > 0 && cores_len < cores_cap) {
      core_list_item_t *p = (core_list_item_t *) realloc(cores, cores_len * sizeof(core_list_item_t));
      if (p) { cores = p; cores_cap = cores_len; }
    }
  }
  // sort by core order number
  cores_sort(cores, cores_len);
  
  autoload_enabled = false;
  // pre-select autoload core
  char autoload_id[sizeof(hw_setup.autoload_core)];
  strcpy(autoload_id, hw_setup.autoload_core); kg_trim(autoload_id);
  for (uint8_t i=0; i<cores_len; i++) {
    if (!autoload_enabled && hw_setup.autoload_enabled && strcmp(cores[i].id, autoload_id) == 0) {
      autoload_enabled = true;
      autoload_countdown = hw_setup.autoload_timeout;
      core_sel = i;
      autoload_timer = ms_now();
    }
  }

}

// sound: the start sound of the graphic menu (a core chosen by the user, not the autoload)
static void start_core(bool sound) {
  autoload_enabled = false;
  if (has_ft && sound) {
    app_core_browser_ft_menu(2); // play wav
  }
  d_printf("Selected core %s to boot from menu", cores[core_sel].filename); d_println();
  has_ft = false;
  do_configure(cores[core_sel].filename);
  switch (core.type) {
    case CORE_TYPE_BOOT: osd_state = state_core_browser; break;
    case CORE_TYPE_OSD: osd_state = state_main; break;
    case CORE_TYPE_FILELOADER: osd_state = state_file_loader; break;
    default: osd_state = state_main;
  }
}

// the graphic setup on the FT812, else the classic one
static void open_setup() {
  autoload_enabled = false;
  setup_focus = false;
  osd_return_state = state_core_browser;
  osd_state = has_ft ? state_ft_setup : state_setup;
}

// the graphic browser with the keys on the Setup button: Enter opens it, Right / Up / Down go
// back to the cores. true if the key was taken.
static bool setup_focus_key(uint8_t key) {
  if (key == KEY_ENTER) {
    open_setup();
    return true;
  }
  if (key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN) {
    setup_focus = false;
    app_core_browser_ft_menu(1);
    return true;
  }
  return false;
}

void app_core_browser_on_keyboard() {
  uint8_t key = ui_nav_key();
  if (has_ft) ft_mouse_hide(); // the keys move the selection
  if (!has_ft) setup_focus = false;
  if (setup_focus && setup_focus_key(key)) return;
  if (cores_len > 0) {
    if (key == KEY_LEFT && has_ft && core_sel == 0) {
      // already on the first core: onto the Setup button (not by a held Left)
      autoload_enabled = false;
      if (!ui_repeat) setup_focus = true;
    } else {
      // up / down, left / right a page
      uint16_t sel = core_sel;
      if (ui_list_key(&sel, cores_len, page_size(), key)) autoload_enabled = false;
      core_sel = sel;
    }

    if (key == KEY_ENTER) {
      start_core(true);
    }

    // enter setup mode (S, joy start)
    if (key == KEY_S || ((joyL | joyR) & SC_BTN_START)) {
      open_setup();
    }

    // redraw core browser on keypress
    if (osd_state == state_core_browser) {
      if (has_ft) {
        app_core_browser_ft_menu(1);
      } else {
        app_core_browser_menu(APP_COREBROWSER_MENU_OFFSET);
      }
    }
  }

  // return back to classic osd
  if (key == KEY_SPACE && has_ft) {
    has_ft = false;
    ft_power_down(); // the running FT812 hums into the audio
    app_core_browser_overlay();
  }

  // the graphic menu, also when it is off in karabas.ini (until the next start)
  if (key == KEY_G && !has_ft) {
    autoload_enabled = false;
    ft_spi(true);
    has_ft = ft_init(hw_setup.ft_video_mode);
    if (has_ft) {
      d_printf("FT81x started (system clock %lu kHz), switching to FT OSD", (unsigned long) (ft_init_clock_hz() / 1000)); d_println();
      ft_vga(true);
      app_core_browser_ft_overlay();
    } else {
      d_println("FT81x did not come up");
      ft_power_down();
    }
  }
}

// the mouse on the graphic browser: the pointer hovers, a click selects a core, a click on the
// selected one starts it; the wheel moves the selection
void app_core_browser_tick() {
  if (!has_ft) return;
  ft_mouse_event_t ev;
  if (!ft_mouse_poll(&ev)) return;
  bool act = ev.pressed || ev.wheel;
  if (act) autoload_enabled = false;
  if (act) setup_focus = false; // the mouse picks
  if (ev.wheel && cores_len > 0) {
    int sel = core_sel - ev.wheel;
    core_sel = sel < 0 ? 0 : sel >= cores_len ? cores_len - 1 : sel;
  }
  if (ev.pressed & FT_MOUSE_LEFT) {
    int16_t id = ft_hover();
    if (id >= 0 && id < cores_len) {
      if (id == core_sel) {
        start_core(true);
        return;
      }
      core_sel = id;
    } else if (id == HIT_SETUP) {
      open_setup();
      return;
    } else if (id == HIT_PREV || id == HIT_NEXT) {
      // a page, as Left / Right
      uint16_t sel = core_sel;
      kg_list_move(&sel, cores_len, ft_core_page_size(), id == HIT_PREV ? KG_LIST_PAGE_UP : KG_LIST_PAGE_DOWN);
      core_sel = sel;
    }
  }
  if (act || ft_hover() != hover) {
    app_core_browser_ft_menu(act && hw_setup.ft_click ? 1 : 0);
  } else if (ev.moved) {
    ft_pointer_frame();
  }
}

void app_core_browser_on_time() {
  if (autoload_enabled) {
    if (ms_since(autoload_timer) > autoload_countdown * 1000) {
      start_core(false);
    }
  }
}
