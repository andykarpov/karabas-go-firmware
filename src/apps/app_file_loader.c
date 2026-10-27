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
#include "kg_list.h"

// the dir of the core's files (core.dir), trimmed and from the root: "" -> "/", "nes" -> "/nes"
static void core_dir_path(char *out, size_t size) {
  char dir[sizeof(core.dir)];
  strcpy(dir, core.dir);
  kg_trim(dir);
  snprintf(out, size, "%s%s", dir[0] == '/' ? "" : "/", dir);
}

void app_file_loader_read_list(bool forceIndex) {

  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_frame(8,8,24,12);
  osd_fill(9,9,23,11, 32);
  osd_set_color(OSD_COLOR_GREEN_I, OSD_COLOR_BLACK);
  osd_set_pos(9, 9);
  osd_print("Scanning files");
  osd_set_color(OSD_COLOR_MAGENTA_I, OSD_COLOR_BLACK);
  osd_set_pos(9,10);
  osd_print("Please wait...");
  osd_update();
  ms_delay(100);

  // files from sd1 card

  if (has_sd) {
    char d[256];
    core_dir_path(d, sizeof(d));
    if (!sd_opendir(&root1, d)) {

        osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
        osd_frame(8,8,24,12);
        osd_fill(9,9,23,11, 32);
        osd_set_color(OSD_COLOR_RED_I, OSD_COLOR_BLACK);
        osd_set_pos(9, 9);
        osd_print("Error occured");
        osd_set_pos(9,10);
        char b[255];
        snprintf(b, sizeof(b), "Bad dir %s", d);
        osd_print(b);
        osd_update();
        ms_delay(1000);
        app_file_loader_read_list(forceIndex);
        return;
    }
    d_println("Read file list");
    files_clear();
    file_sel = 0;
    char e[sizeof(core.file_extensions)];
    strcpy(e, core.file_extensions);
    kg_str_lower(e); kg_trim(e);
    FILINFO fi;
    while (f_readdir(&root1, &fi) == FR_OK && fi.fname[0]) {
      const char *filename = fi.fname;
      uint16_t id;
      if (!(fi.fattrib & AM_DIR) && sd_file_id(&fi, &id) && kg_name_has_ext(filename, e)) {
        file_list_sort_item_t *item = files_append();
        if (item) {
          memcpy(item->hash, filename, SORT_HASH_LEN);
          item->file_id = id;
        }
      }
    }

  } else {
    osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
    osd_frame(8,8,24,12);
    osd_fill(9,9,23,11, 32);
    osd_set_color(OSD_COLOR_RED_I, OSD_COLOR_BLACK);
    osd_set_pos(9, 9);
    osd_print("Error occured");
    osd_set_pos(9,10);
    osd_print("Check SD card");
    osd_update();
    ms_delay(1000);
    app_file_loader_read_list(forceIndex);
    return;
  }

  // sort by file name
  files_sort(files, files_len);
  
  // cleanup error messages
  if (has_sd) {
    osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
    osd_fill(8,8,24,12, 32);
    osd_update();
  }

  // select and load prev file on boot
  if (has_sd && files_len > 0 && core.last_file_id > 0) {
    //d_printf("Looking for last file id = %d", core.last_file_id); d_println();
    for (uint16_t i=0; i<files_len; i++) {
      if (core.last_file_id == files[i].file_id) {
          file_sel = i;
          //d_printf("Found %d pos", i); d_println();
          FILINFO fi;
          if (sd_find_id(&root1, core.last_file_id, &fi)) {
            app_file_loader_send_file(core.last_file_id);
          }
          // hide osd
          if (!is_osd_hiding) {
            is_osd_hiding = true;
            hide_timer = ms_now();
            osd_hide_menu();
          }
      }
    }
  }
}

void app_file_loader_menu(uint8_t vpos) {
  file_pages = kg_list_pages(files_len, file_page_size);
  file_page = kg_list_page(file_sel, file_page_size);
  uint16_t file_from = (file_page-1)*file_page_size;
  uint16_t file_to = file_page*file_page_size > files_len ? files_len : file_page*file_page_size;
  bool cached = cached_file_from == file_from && cached_file_to == file_to;

  for (uint16_t row = 0; row < file_page_size; row++) {
    uint16_t i = file_from + row;
    char name[33] = "";
    if (i < file_to) {
      if (cached) {
        memcpy(name, cached_names[row].name, sizeof(name));
      } else {
        FILINFO fi;
        if (sd_find_id(&root1, files[i].file_id, &fi)) snprintf(name, sizeof(name), "%s", fi.fname);
        memcpy(cached_names[row].name, name, sizeof(name));
      }
    }
    ui_row(0, vpos + row, name, i == file_sel && i < file_to);
  }
  cached_file_from = file_from;
  cached_file_to = file_to;

  char b[32];
  snprintf(b, sizeof(b), "Page %03u of %03u", file_page, file_pages);
  ui_row(0, vpos + file_page_size + 1, "", false);
  osd_set_pos(8, vpos + file_page_size + 1); osd_print(b);
  osd_update();
}

void app_file_loader_overlay(bool initSD, bool recreateIndex) {
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_clear();
  osd_header(core.build, core.id, HW_ID);
  print_time();

  // collect files from sd
  if (initSD) {
      app_file_loader_read_list(recreateIndex);
  }

  app_file_loader_menu(APP_COREBROWSER_MENU_OFFSET);
  osd_line(21);
  ui_hint("Please use arrows to navigate", "Press Enter to load selection");
  osd_update();
}

void app_file_loader_save()
{
    if (!sd_open(&file1, core.filename, FA_READ | FA_WRITE)) {
      d_println("Unable to open bitstream file to write");
      return;
    }
    core.last_file_id = files[file_sel].file_id;
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
    app_file_loader_overlay(false, false);
    return;
  }

  const char *game_name = fi.fname;
  bk_set_game(core.dir, game_name); // its save, if the core asks for one after the load

  uint8_t buf[512];
  int c;

  // keep the whole file transfer together on the fpga link
  spi_bus_lock();

  // trigger reset
  spi_send(CMD_FILELOADER, 0, 3); // 11 - active = 1, reset = 1
  spi_send(CMD_FILELOADER, 0, 2); // 10 - active = 1, reset = 0

  uint64_t file_size = f_size(&file1);

  d_printf("Sending file %d (%lu bytes)", file_id, (unsigned long) file_size); d_println();
  d_flush();
  
uint32_t cnt = 0;
uint32_t c1 = 0;
  uint32_t send_timer = ms_now();
  send_timer = ms_now();
  uint32_t crc = KG_CRC32_INIT; // debug: compare with the crc32 of the file on a pc

  while((c = sd_read(&file1, buf, sizeof(buf))) > 0) {
    if (hw_setup.debug_enabled) crc = kg_crc32_update(crc, buf, c);
    // bursts if the core supports them; classic frames for whatever they did not send
    size_t done = link_burst_write(CMD_FILEDATA, cnt, buf, c);
    for (int j=done; j<c; j++) {
      app_file_loader_send_byte(cnt + j, buf[j], j == (int) done);
    }
    cnt += c;
    c1 += c;
    if (c1 >= 16384) {
      c1 = 0;
      osd_loading_popup(cnt, file_size);
      osd_update();
    }
  }
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
  app_file_loader_overlay(false, false);
}

void app_file_loader_on_keyboard() {
  if (files_len == 0) return;
  uint8_t key = ui_nav_key();

  // up / down, left / right a page
  ui_list_key(&file_sel, files_len, file_page_size, key);

  // R = recreate file index
  if (key == KEY_R) {
    file_sel = 0;
    app_file_loader_overlay(true, true);
  }

  if (key == KEY_ENTER) {
    app_file_loader_save();
    app_file_loader_send_file(files[file_sel].file_id);
    // hide osd
    if (!is_osd_hiding) {
      is_osd_hiding = true;
      hide_timer = ms_now();
      osd_hide_menu();
    }
  }

  // redraw file loader on keypress
  if (osd_state == state_file_loader) {
    app_file_loader_menu(APP_COREBROWSER_MENU_OFFSET);
  }
}

void app_file_loader_send_byte(uint32_t addr, uint8_t data, bool force_bank) {
  // send file bank address every 256 bytes (and when resuming in the middle of a bank)
  if (force_bank || (addr & 0x000000FF) == 0) {
    uint8_t filebank3 = (uint8_t)((addr & 0xFF000000) >> 24);
    uint8_t filebank2 = (uint8_t)((addr & 0x00FF0000) >> 16);
    uint8_t filebank1 = (uint8_t)((addr & 0x0000FF00) >> 8);
    //d_printf("File bank %d %d %d", filebank1, filebank2, filebank3); d_println();
    spi_send(CMD_FILEBANK, 0, filebank1);
    spi_send(CMD_FILEBANK, 1, filebank2);
    spi_send(CMD_FILEBANK, 2, filebank3);
  }
  // send lower 256 bytes
  uint8_t fileaddr = (uint8_t)((addr & 0x000000FF));
  spi_send(CMD_FILEDATA, fileaddr, data);
}

