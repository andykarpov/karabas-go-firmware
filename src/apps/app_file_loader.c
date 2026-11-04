#include "crc32.h"
#include "config.h"
#include "types.h"
#include "main.h"
#include "osd.h"
#include "input/usb_hid_keys.h"
#include "input/sega.h"
#include "apps/app_file_loader.h"
#include "ui/sorts.h"
#include "file_name.h"
#include "ui/ui_draw.h"
#include "ui/file_browser.h"

// The dirs: core.dir (make_core "dir") is where the browser starts, the user can go anywhere on
// the card from it ("/", "../", the subdirs; Backspace up, H back to core.dir). The dir of the
// last loaded file is kept in karabas.ini, [fileloader] <core id>=<dir> (no entry, or empty: the
// start dir), and the id of the file in the core file (FILE_POS_FILELOADER_FILE): an id is an
// index in its dir, so the two are written together, the ini first.
#define INI_SECTION "fileloader"

static char start_dir[KG_PATH_MAX]; // core.dir, from the root
static char cur_dir[KG_PATH_MAX];   // the dir of the list
static char ini_dir[KG_PATH_MAX];   // the dir saved in karabas.ini ("" = the start dir)
static bool dir_pending = false;    // cur_dir is read with the first list of a core

// a dir from the root, without '/' runs and the '/' at its end: "nes/" -> "/nes"
static void dir_norm(char *dir, size_t size) {
  kg_trim(dir);
  kg_path_root(dir, size);
  kg_path_collapse(dir);
  size_t n = strlen(dir);
  while (n > 1 && dir[n - 1] == '/') dir[--n] = '\0';
}

// the key of the core in karabas.ini: its id
static void ini_key(char *out, size_t size) {
  snprintf(out, size, "%s", core.id);
  kg_trim(out);
}

// kgo2: the last file by its name (in ini_dir); its id is found when the list is read
static char last_name[256];

// the start dir and the dir of the last file (kgo1: karabas.ini; kgo2: the default file of the
// core, the user's last file comes with step 3), if it can be opened
static void dirs_init(void) {
  snprintf(start_dir, sizeof(start_dir), "%s", core.dir);
  dir_norm(start_dir, sizeof(start_dir));
  snprintf(cur_dir, sizeof(cur_dir), "%s", start_dir);
  ini_dir[0] = '\0';
  last_name[0] = '\0';
  if (core.format == CORE_FORMAT_KGO2) {
    if (!core.loader_file[0]) return;
    snprintf(ini_dir, sizeof(ini_dir), "%s", core.loader_file);
    const char *slash = strrchr(core.loader_file, '/');
    snprintf(last_name, sizeof(last_name), "%s", slash ? slash + 1 : core.loader_file);
    kg_path_parent(ini_dir);
    dir_norm(ini_dir, sizeof(ini_dir));
    snprintf(cur_dir, sizeof(cur_dir), "%s", ini_dir);
    return;
  }
  char key[sizeof(core.id)];
  ini_key(key, sizeof(key));
  if (!key[0] || !settings_ini_get(INI_SECTION, key, ini_dir, sizeof(ini_dir))) ini_dir[0] = '\0';
  if (!ini_dir[0]) return;
  dir_norm(ini_dir, sizeof(ini_dir));
  if (sd_opendir(&root1, ini_dir)) {
    snprintf(cur_dir, sizeof(cur_dir), "%s", ini_dir);
  } else {
    d_printf("File loader: no dir %s, back to %s", ini_dir, start_dir); d_println();
    core.last_file_id = 0; // its id was in that dir
  }
}

// dir is the one of the last file loaded
static bool ini_dir_is(const char *dir) {
  return strcmp(dir, ini_dir[0] ? ini_dir : start_dir) == 0;
}

// The list of the core's files is in the file browser (fb_*), which the F / FL items of the core
// menu (Options) fill too: list_loaded is cleared when the menu is opened, and the list is read
// again on the way back, the cursor on the file it was on (sel_id). The last file is loaded only
// with the first list of a core (autoload), not with a list read again.
static bool list_loaded = false;
static bool autoload = false;
static uint16_t sel_id = 0;
static char sel_name[256]; // a dir to put the cursor on (the one we came up from), by name

void app_file_loader_core_loaded(void) {
  list_loaded = false;
  autoload = true;
  sel_id = 0;
  sel_name[0] = '\0';
  dir_pending = true;
}

// a message box shown for a second: no SD card, a dir that can't be opened
static void read_error(const char *line2) {
  ui_box("Error occured", OSD_COLOR_RED_I, line2, OSD_COLOR_RED_I);
  osd_update();
  ms_delay(1000);
}

void app_file_loader_read_list(void) {

  ui_box("Scanning files", OSD_COLOR_GREEN_I, "Please wait...", OSD_COLOR_MAGENTA_I);
  osd_update();
  ms_delay(100);

  if (dir_pending && has_sd) {
    dir_pending = false;
    dirs_init();
  }

  // the root, the parent, the subdirs and the files with the extensions of the core, from sd1;
  // without the card the list stays empty (the hotkeys still work), a dir that can't be opened
  // keeps "/" and "../" to go elsewhere
  bool opened = has_sd && sd_opendir(&root1, cur_dir);
  if (!has_sd) {
    read_error("Check SD card");
  } else if (!opened) {
    char b[UI_BOX_TEXT + 1];
    snprintf(b, sizeof(b), "Bad dir %s", cur_dir);
    d_println(b);
    read_error(b);
  }
  d_printf("Read file list of %s", cur_dir); d_println();
  fb_clear();
  if (has_sd) {
    fb_add_root();
    if (strlen(cur_dir) > 1) fb_add_parent();
  }
  uint16_t name_id = 0;
  bool in_last_dir = ini_dir_is(cur_dir);
  FILINFO fi;
  while (opened && f_readdir(&root1, &fi) == FR_OK && fi.fname[0]) {
    uint16_t id;
    if (sd_is_hidden(&fi) || !sd_file_id(&fi, &id)) continue;
    if (fi.fattrib & AM_DIR) {
      if (!fb_add_dir(fi.fname, id)) continue;
      if (sel_name[0] && strcasecmp(sel_name, fi.fname) == 0) name_id = id;
    } else if (kg_name_has_ext(fi.fname, core.file_extensions)) {
      fb_add_file(fi.fname, id);
      // kgo2: the id of the last file, by its name
      if (in_last_dir && last_name[0] && strcmp(last_name, fi.fname) == 0) core.last_file_id = id;
    }
  }
  sel_name[0] = '\0';

  // dirs first, by name
  fb_sort();
  
  ui_box_clear();
  osd_update();
  list_loaded = true;

  // the cursor on the dir we came up from, on the entry it was on, else on the last file loaded
  // (in its dir), else on the first entry after "/" and "../"
  uint16_t id = name_id ? name_id : sel_id ? sel_id : in_last_dir ? core.last_file_id : 0;
  sel_id = 0;
  uint16_t first = 0;
  while (first + 1 < fb_len() && fb_id(first) == 0) first++;
  fb_select(first);
  for (uint16_t i = 0; id > 0 && i < fb_len(); i++) {
    if (fb_id(i) == id) { fb_select(i); break; }
  }

  // load the last file with the first list of the core
  bool load = autoload;
  autoload = false;
  if (load && has_sd && core.loader_autoload && in_last_dir && core.last_file_id > 0 && !fb_is_dir(fb_sel()) &&
      fb_id(fb_sel()) == core.last_file_id) {
    if (sd_find_id(&root1, core.last_file_id, &fi)) {
      app_file_loader_send_file(core.last_file_id);
    }
    ui_osd_hide();
  }
}

void app_file_loader_menu(uint8_t vpos) {
  uint8_t y = fb_draw(vpos, cur_dir, true);
  // the core's own menu (Tab or O), left of the page number
  if (core.osd_len > 0) {
    osd_set_pos(0, y);
    ui_hotkey("O", "ptions");
    osd_update();
  }
}

// initSD: read the list again (R); else only when it is not in the file browser
void app_file_loader_overlay(bool initSD) {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_clear();
  osd_header(core.build, core.id, HW_ID);
  print_time();

  // collect files from sd
  if (initSD || !list_loaded) {
    app_file_loader_read_list();
  }

  // the hints first: the dir row and the page number take the row of the hint line
  char b[OSD_SIZE_X + 1] = "Files: ";
  kg_exts_text(core.file_extensions, b + 7, OSD_SIZE_X - 7);
  ui_hint(b[7] ? b : "Please use arrows to navigate", "Enter load/open, Bksp up, H home");
  app_file_loader_menu(APP_COREBROWSER_MENU_OFFSET);
}

// the file of the cursor is the last one: its dir into karabas.ini (only when it changed), its id
// into the core file; an id without its dir in the ini is written as 0 (no autoload)
void app_file_loader_save()
{
    uint16_t id = fb_id(fb_sel());
    if (core.format == CORE_FORMAT_KGO2) {
      // kgo2: kept in memory (the user's last file in /config/<id>.ini comes with step 3)
      FILINFO fi;
      if (sd_opendir(&root1, cur_dir) && sd_find_id(&root1, id, &fi)) {
        snprintf(ini_dir, sizeof(ini_dir), "%s", cur_dir);
        snprintf(last_name, sizeof(last_name), "%s", fi.fname);
        core.last_file_id = id;
      }
      return;
    }
    if (!ini_dir_is(cur_dir)) {
      char key[sizeof(core.id)];
      ini_key(key, sizeof(key));
      // the start dir as an empty value: the core's own dir, also if make_core moves it
      const char *v = strcmp(cur_dir, start_dir) == 0 ? "" : cur_dir;
      if (key[0] && settings_ini_set(INI_SECTION, key, v)) {
        snprintf(ini_dir, sizeof(ini_dir), "%s", v);
      } else {
        d_printf("File loader: the dir %s is not saved", cur_dir); d_println();
        id = 0;
      }
    }
    if (!sd_open(&file1, core.filename, FA_READ | FA_WRITE)) {
      d_println("Unable to open bitstream file to write");
      return;
    }
    core.last_file_id = id;
    file_write16(FILE_POS_FILELOADER_FILE, core.last_file_id);
    sd_close(&file1);
}

void app_file_loader_send_file(uint16_t file_id) {

  osd_loading_popup(0, 100);
  osd_update();

  // the save of the game before: written and closed before the core loads another one
  bk_flush();
  bk_close();

  const char *dirname = cur_dir;
  if (!sd_opendir(&root1, dirname)) {
    d_printf("Unable to open dir %s", dirname); d_println();
    app_file_loader_overlay(false); // the loading popup goes
    return;
  }

  sd_close(&file1);

  FILINFO fi;
  char path[KG_PATH_MAX];
  bool found = sd_find_id(&root1, file_id, &fi);
  if (found) kg_path_join(path, sizeof(path), dirname, fi.fname);
  if (!found || !sd_open(&file1, path, FA_READ)) {
    d_printf("Unable to open file %d", file_id); d_println();
    d_flush();
    app_file_loader_overlay(false);
    return;
  }

  const char *game_name = fi.fname;
  bk_set_game(core.dir, game_name); // its save, if the core asks for one after the load

  // keep the whole file transfer together on the fpga link
  spi_bus_lock();

  // trigger reset
  spi_send(CMD_FILELOADER, 0, 3); // 11 - active = 1, reset = 1
  spi_send(CMD_FILELOADER, 0, 2); // 10 - active = 1, reset = 0

  uint32_t file_size = f_size(&file1);
  d_printf("Sending file %d (%lu bytes)", file_id, (unsigned long) file_size); d_println();
  d_flush();

  uint32_t send_timer = ms_now();
  uint32_t crc = KG_CRC32_INIT; // debug: compare with the crc32 of the file on a pc
  uint32_t cnt = core_send_file(&file1, file_size, CMD_FILEDATA, CMD_FILEBANK, 0, core_loading_popup, &file_size,
    hw_setup.debug_enabled ? &crc : NULL);
  osd_loading_popup(file_size, file_size);
  osd_update();

  // 00 - active = 0, reset = 0
  spi_send(CMD_FILELOADER, 0, 0);
  spi_bus_unlock();

  sd_close(&file1);
  d_printf("Sent %lu bytes in %lu ms (%s), crc32 %08lx", (unsigned long) cnt, (unsigned long) ms_since(send_timer),
    link_burst_available() ? "burst" : "classic", (unsigned long) kg_crc32_final(crc)); d_println();
  ms_delay(100);

  // reload menu
  app_file_loader_overlay(false);
}

// the list of another dir; the cursor on the subdir named child (going up), else on the first entry
static void go_dir(const char *dir, const char *child) {
  char d[KG_PATH_MAX];
  snprintf(d, sizeof(d), "%s", dir);
  dir_norm(d, sizeof(d));
  d_printf("File loader: dir %s", d); d_println();
  snprintf(sel_name, sizeof(sel_name), "%s", child ? child : ""); // child may be in cur_dir
  snprintf(cur_dir, sizeof(cur_dir), "%s", d);
  sel_id = 0;
  app_file_loader_overlay(true);
}

static void go_up(void) {
  char d[KG_PATH_MAX];
  snprintf(d, sizeof(d), "%s", cur_dir);
  const char *slash = strrchr(cur_dir, '/');
  kg_path_parent(d);
  go_dir(d, slash ? slash + 1 : NULL);
}

void app_file_loader_leave(void) {
  if (list_loaded && fb_len() > 0) sel_id = fb_id(fb_sel());
  list_loaded = false; // the browser of a file item takes the list
}

void app_file_loader_on_keyboard() {
  uint8_t key = ui_nav_key();

  // R = recreate file index (also an empty list: the dir may be readable now)
  if (key == KEY_R) {
    app_file_loader_overlay(true);
    return;
  }
  // Tab / O: the core's own menu; Esc on its main page comes back here
  if ((key == KEY_TAB || key == KEY_O) && core.osd_len > 0) {
    app_file_loader_leave();
    osd_state = state_main;
    return;
  }

  // H: back to the dir of the core; backspace: the parent dir
  if (key == KEY_H && has_sd && strcmp(cur_dir, start_dir) != 0) {
    go_dir(start_dir, NULL);
    return;
  }
  if (key == KEY_BACKSPACE && strlen(cur_dir) > 1) {
    go_up();
    return;
  }

  if (fb_len() == 0) return;

  // up / down, left / right a page
  fb_key(key);

  if (key == KEY_ENTER) {
    uint16_t sel = fb_sel();
    FILINFO fi;
    if (fb_is_root(sel)) {
      go_dir("/", NULL);
      return;
    }
    if (fb_is_parent(sel)) {
      go_up();
      return;
    }
    if (fb_is_dir(sel)) {
      if (sd_opendir(&root1, cur_dir) && sd_find_id(&root1, fb_id(sel), &fi)) {
        char path[KG_PATH_MAX];
        kg_path_join(path, sizeof(path), cur_dir, fi.fname);
        go_dir(path, NULL);
      }
      return;
    }
    app_file_loader_save();
    app_file_loader_send_file(fb_id(sel));
    ui_osd_hide();
  }

  // redraw file loader on keypress
  if (osd_state == state_file_loader) {
    app_file_loader_menu(APP_COREBROWSER_MENU_OFFSET);
  }
}
