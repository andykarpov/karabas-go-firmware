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

// the dir of the core's files (core.dir), trimmed and from the root: "" -> "/", "nes" -> "/nes"
static void core_dir_path(char *out, size_t size) {
  char dir[sizeof(core.dir)];
  strcpy(dir, core.dir);
  kg_trim(dir);
  snprintf(out, size, "%s%s", dir[0] == '/' ? "" : "/", dir);
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

  // files from sd1 card; without the card or the dir the list stays empty (the hotkeys still work)
  char d[256];
  core_dir_path(d, sizeof(d));
  bool opened = has_sd && sd_opendir(&root1, d);
  if (!has_sd) {
    read_error("Check SD card");
  } else if (!opened) {
    char b[UI_BOX_TEXT + 1];
    snprintf(b, sizeof(b), "Bad dir %s", d);
    d_println(b);
    read_error(b);
  }
  d_println("Read file list");
  fb_clear();
  char e[sizeof(core.file_extensions)];
  strcpy(e, core.file_extensions);
  kg_str_lower(e); kg_trim(e);
  FILINFO fi;
  while (opened && f_readdir(&root1, &fi) == FR_OK && fi.fname[0]) {
    uint16_t id;
    if (!(fi.fattrib & AM_DIR) && sd_file_id(&fi, &id) && kg_name_has_ext(fi.fname, e)) {
      fb_add_file(fi.fname, id);
    }
  }

  // sort by file name
  fb_sort();
  
  ui_box_clear();
  osd_update();

  // select and load prev file on boot
  if (has_sd && fb_len() > 0 && core.last_file_id > 0) {
    for (uint16_t i=0; i<fb_len(); i++) {
      if (core.last_file_id != fb_id(i)) continue;
      fb_select(i);
      FILINFO fi;
      if (sd_find_id(&root1, core.last_file_id, &fi)) {
        app_file_loader_send_file(core.last_file_id);
      }
      ui_osd_hide();
      break;
    }
  }
}

void app_file_loader_menu(uint8_t vpos) {
  char d[256];
  core_dir_path(d, sizeof(d));
  fb_draw(vpos, d, false);
}

void app_file_loader_overlay(bool initSD) {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_clear();
  osd_header(core.build, core.id, HW_ID);
  print_time();

  // collect files from sd
  if (initSD) {
    app_file_loader_read_list();
  }

  app_file_loader_menu(APP_COREBROWSER_MENU_OFFSET);
  ui_hint("Please use arrows to navigate", "Press Enter to load selection");
  osd_update();
}

void app_file_loader_save()
{
    if (!sd_open(&file1, core.filename, FA_READ | FA_WRITE)) {
      d_println("Unable to open bitstream file to write");
      return;
    }
    core.last_file_id = fb_id(fb_sel());
    file_write16(FILE_POS_FILELOADER_FILE, core.last_file_id);
    sd_close(&file1);
}

void app_file_loader_send_file(uint16_t file_id) {

  osd_loading_popup(0, 100);
  osd_update();

  // the save of the game before: written and closed before the core loads another one
  bk_flush();
  bk_close();

  char dirname[255];
  core_dir_path(dirname, sizeof(dirname));
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

void app_file_loader_on_keyboard() {
  uint8_t key = ui_nav_key();

  // R = recreate file index (also an empty list: the dir may be readable now)
  if (key == KEY_R) {
    app_file_loader_overlay(true);
    return;
  }
  if (fb_len() == 0) return;

  // up / down, left / right a page
  fb_key(key);

  if (key == KEY_ENTER) {
    app_file_loader_save();
    app_file_loader_send_file(fb_id(fb_sel()));
    ui_osd_hide();
  }

  // redraw file loader on keypress
  if (osd_state == state_file_loader) {
    app_file_loader_menu(APP_COREBROWSER_MENU_OFFSET);
  }
}
