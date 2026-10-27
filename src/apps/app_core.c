#include "types.h"
#include "config.h"
#include "osd.h"
#include "input/usb_hid_keys.h"
#include "input/sega.h"
#include "apps/app_core.h"
#include "ui/sorts.h"
#include "file_name.h"
#include "ui/ui_draw.h"
#include "ui/file_browser.h"
#include "kg_list.h"

uint8_t curr_osd_item;
bool is_filebrowser = false;
int filebrowser_slot = -1;
int prev_filebrowser_slot = -1;
bool prev_is_filebrowser = false;
// the dir of the slot when the file browser was opened: browsing changes the dir of the slot,
// esc puts it back, so it stays the dir of the mounted image
static char filebrowser_dir[256];

// the file mounter slot of the image chosen last: a slot without an image opens its browser in
// that dir (a game on several disks: DF1 starts where the disk of DF0 is)
static int last_mount_slot = -1;
// the image of that slot: the browser of the empty slot preselects the file after it (disk 2)
static char presel_after[256];

static void filebrowser_open() {
  uint8_t slot = core.osd[curr_osd_item].slot_id;
  core_file_slot_t *fs = &file_slots[slot];
  memcpy(filebrowser_dir, fs->dir, sizeof(filebrowser_dir));
  presel_after[0] = '\0';
  if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILEMOUNTER && !fs->is_mounted) {
    // the last chosen image of a slot with the same extensions, else any mounted one
    int from = -1;
    for (uint8_t i = 0; i < core.osd_len; i++) {
      if (core.osd[i].type != CORE_OSD_TYPE_FILEMOUNTER) continue;
      uint8_t s = core.osd[i].slot_id;
      if (s == slot || !file_slots[s].is_mounted || strcmp(file_slots[s].ext, fs->ext) != 0) continue;
      if (from == -1 || s == last_mount_slot) from = s;
    }
    if (from != -1 && strcmp(file_slots[from].dir, fs->dir) != 0) {
      memcpy(fs->dir, file_slots[from].dir, sizeof(fs->dir));
      filebrowser_slot = -1; // read the file list of that dir
    }
    if (from != -1) {
      memcpy(presel_after, file_slots[from].filename, sizeof(presel_after));
      filebrowser_slot = -1;
    }
  }
  is_filebrowser = true;
}

// eject the image of a file mounter item: the core gets size 0, the core file forgets the image
static void app_core_eject(uint8_t pos) {
  if (core.osd[pos].type != CORE_OSD_TYPE_FILEMOUNTER) return;
  core_file_slot_t *fs = &file_slots[core.osd[pos].slot_id];
  d_printf("Eject slot %d", core.osd[pos].slot_id); d_println();
  fs->filename[0] = '\0';
  fs->is_mounted = false;
  img_mount(core.osd[pos].slot_id);
  filebrowser_slot = -1; // the file list has no eject entry now
  core.osd_need_save = true;
}

// make the F item pos the physical floppy drive DS0 / DS1; a drive is in one slot only, so
// another slot on it is ejected
static void app_core_select_phys(uint8_t pos, int drive) {
  for (uint8_t i = 0; i < core.osd_len; i++) {
    if (i != pos && core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER && file_slots[core.osd[i].slot_id].is_mounted &&
        kg_phys_drive(file_slots[core.osd[i].slot_id].filename) == drive) {
      app_core_eject(i);
    }
  }
  core_file_slot_t *fs = &file_slots[core.osd[pos].slot_id];
  d_printf("Slot %d: physical drive DS%d", core.osd[pos].slot_id, drive); d_println();
  kg_phys_name(drive, fs->filename);
  fs->is_mounted = true;
  img_mount(core.osd[pos].slot_id); // also a disk change for the core
  filebrowser_slot = -1;
  core.osd_need_save = true;
}

uint8_t find_first_item() {
  if (core.osd_len > 0) {
    for (uint8_t i = 0; i < core.osd_len; i++) {
      if (core.osd[i].options_len > 0 || core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
        return i;
      }
    }
  }
  return 0;
}

uint8_t find_last_item() {
  if (core.osd_len > 0) {
    for (uint8_t i = core.osd_len-1; i >= 0; i--) {
      if (core.osd[i].options_len > 0 || core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
        return i;
      }
    }
  }
  return 0;
}

uint8_t find_prev_item() {
  uint8_t first = find_first_item();
  uint8_t last = find_last_item();

  // border case with 0 or 1 items
  if (first == last) {
    return first;
  }

  // if there is a room to decrease - find a previous item
  if (curr_osd_item > first) {
    for (uint8_t i = curr_osd_item-1; i >= first; i--) {
      if (core.osd[i].options_len > 0 || core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
        return i;
      }
    }
  }
  // fallback - return last item with options
  return last;
}

uint8_t find_next_item() {
  uint8_t first = find_first_item();
  uint8_t last = find_last_item();

  // border case with 0 or 1 items
  if (first == last) {
    return last;
  }

  // if there is a room to increase - find a next item
  if (curr_osd_item < last) {
    for (uint8_t i = curr_osd_item+1; i <= last; i++) {
      if (core.osd[i].options_len > 0 || core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
        return i;
      }
    }
  }
  // fallback - return first item with options
  return first;

}

void app_core_overlay(bool force)
{
  osd_state = state_main;

  // disable popup in overlay mode
  osd_hide_popup();

  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_clear();

  osd_header(core.build, core.id, HW_ID);
  print_time();

  if (force) {
    curr_osd_item = find_first_item();
  }
  app_core_menu(APP_COREBROWSER_MENU_OFFSET);

  // footer
  if (!is_filebrowser) {
    osd_set_pos(0, 23);
    ui_hotkey("S", "etup ");
    ui_hotkey("A", "bout");
    // file mounter items: delete ejects the image
    for (uint8_t i=0; i<core.osd_len; i++) {
      if (core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER) {
        osd_print(" ");
        ui_hotkey("Del", " eject");
        break;
      }
    }
  }
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK); 
  osd_line(24);
  osd_set_pos(0,25); osd_print("Press Menu+Esc to toggle OSD");
  osd_update();
}

void app_core_menu(uint8_t vpos) {
  for (uint8_t i=0; i<core.osd_len; i++) {
    const char *name = core.osd[i].name;
    const char *hotkey = core.osd[i].hotkey;
    char text[33];
    osd_set_pos(0, i+vpos);
    osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
    // text line (32 chars)
    if (core.osd[i].type == CORE_OSD_TYPE_TEXT) {
      snprintf(text, sizeof(text), "%s%s", name, hotkey);
      osd_print(text);
    // normal osd line (name, option, hotkey)
    } else {
      snprintf(text, 11, "%s", name);
      osd_print(text);

      char option[11];
      if (core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
        int phys = kg_phys_drive(file_slots[core.osd[i].slot_id].filename);
        if (file_slots[core.osd[i].slot_id].is_mounted && phys >= 0) {
          strcpy(option, phys ? "Phys DF1" : "Phys DF0");
        } else if (file_slots[core.osd[i].slot_id].is_mounted) {
          kg_short_name(file_slots[core.osd[i].slot_id].filename, option, 10); // long names: "Princ~.trd"
        } else {
          strcpy(option, "-NO IMAGE-");
        }
      } else {
        snprintf(option, sizeof(option), "%-10s", core.osd[i].options[core.osd[i].val].name);
      }
      osd_set_pos(11, i+vpos);
      if (curr_osd_item == i) {
        osd_set_color(OSD_COLOR_BLACK, OSD_COLOR_YELLOW_I);
      } else {
        osd_set_color(OSD_COLOR_YELLOW_I, OSD_COLOR_BLACK);
      }
      osd_print(option);

      osd_set_color(OSD_COLOR_CYAN_I, OSD_COLOR_BLACK);
      if (core.osd[i].options_len > 0 || core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
        osd_set_pos(22, i+vpos);
        snprintf(text, 11, "%s", hotkey);
        osd_print(text);
      } else {
        osd_set_pos(11, i+vpos);
        osd_print(hotkey);
      }
    }
  }
  if (core.osd_len <= 19) osd_fill(0, 23, 31, 23, ' ');
  if (core.osd_len <= 18) osd_fill(0, 22, 31, 22, ' ');
  if (core.osd_len <= 17) osd_fill(0, 21, 31, 21, ' ');
  // no osd_update() here: app_core_overlay() draws the footer over these rows first, else the
  // cleared footer went out and came back on every move
}

void app_core_save(uint8_t pos)
{
    if (!sd_open(&file1, core.filename, FA_READ | FA_WRITE)) {
      d_println("Unable to open bitstream file to write");
      return;
    }
    core.osd_need_save = false;
    if (core.osd[pos].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[pos].type == CORE_OSD_TYPE_FILELOADER) {
      // save file mounter data (dir, filename) into the core
      uint32_t offset = file_slots[core.osd[pos].slot_id].offset_dir;
      file_seek(offset); 
      file_write_buf(file_slots[core.osd[pos].slot_id].dir, 256);
      offset = file_slots[core.osd[pos].slot_id].offset_filename;
      file_seek(offset); 
      file_write_buf(file_slots[core.osd[pos].slot_id].filename, 256);
    } else {
      // save switch state into the core
      core.osd[pos].prev_val = core.osd[pos].val;
      file_seek(FILE_POS_SWITCHES_DATA + pos);
      sd_write(&file1, &core.osd[pos].val, 1);
    }
    sd_close(&file1);
}

void app_core_on_keyboard() {

  uint8_t key = ui_nav_key();

  if (is_filebrowser) {

    // up / down, left / right a page
    if (fb_key(key)) need_redraw = true;

    // enter
    if (key == KEY_ENTER) {
      uint16_t sel = fb_sel();
      if (fb_len() > 0) {
        sd_close(&file1);
        FILINFO fi;
        // eject
        if (fb_is_eject(sel)) {
          app_core_eject(curr_osd_item);
          is_filebrowser = false;
        }
        // physical floppy drive
        else if (fb_phys(sel) >= 0) {
          app_core_select_phys(curr_osd_item, fb_phys(sel));
          is_filebrowser = false;
        }
        // goto root (.)
        else if (fb_is_root(sel)) {
          strcpy(file_slots[core.osd[curr_osd_item].slot_id].dir, "/");
          // re-read files from root (the image stays mounted until a file is selected)
          app_core_init_filebrowser();
          app_core_filebrowser(APP_COREBROWSER_MENU_OFFSET);
          return;
        }
        // goto parent (..)
        else if (fb_is_parent(sel)) {
          char *dir = file_slots[core.osd[curr_osd_item].slot_id].dir;
          kg_path_parent(dir);
          kg_path_collapse(dir);
          d_print("Enter directory "); d_print(dir); d_println();
          // re-read files from parent dir
          app_core_init_filebrowser();
          app_core_filebrowser(APP_COREBROWSER_MENU_OFFSET);
          return;
        }
        // open file / dir
        else if (sd_find_id(&root1, fb_id(sel), &fi)) {
          // goto new dir name
          if (fi.fattrib & AM_DIR) {
            const char *dirname = fi.fname;
            char *dir = file_slots[core.osd[curr_osd_item].slot_id].dir;
            char path[KG_PATH_MAX];
            kg_path_join(path, sizeof(path), dir, dirname);
            kg_path_collapse(path);
            snprintf(dir, sizeof(file_slots[0].dir), "%s", path);
            app_core_init_filebrowser();
            app_core_filebrowser(APP_COREBROWSER_MENU_OFFSET);
            return;
          }
          // file selection (if ext matches)
          else {
            d_printf("Selecting file id %d", fb_id(sel)); d_println();
            const char *filename = fi.fname;
            d_printf("Filename %s", filename);
            // check file ext match
            char exts[sizeof(file_slots[0].ext)];
            strcpy(exts, file_slots[core.osd[curr_osd_item].slot_id].ext);
            kg_trim(exts);
            if (kg_name_has_ext(filename, exts)) {
              strcpy(file_slots[core.osd[curr_osd_item].slot_id].filename, filename);
              file_slots[core.osd[curr_osd_item].slot_id].is_mounted = true;
              if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILEMOUNTER) {
                last_mount_slot = core.osd[curr_osd_item].slot_id;
              }
              app_core_on_select_file();
              is_filebrowser = false; // exit from file browser mode
              filebrowser_slot = -1; // the file list gets the eject entry
            } else {
              d_println("File extension does not match");
            }
          }
        }

        core.osd_need_save = true;
      }
      need_redraw = true;
    }

    // esc: back to the dir of the mounted image
    if (key == KEY_ESC) {
      core_file_slot_t *fs = &file_slots[core.osd[curr_osd_item].slot_id];
      if (strcmp(fs->dir, filebrowser_dir) != 0) {
        memcpy(fs->dir, filebrowser_dir, sizeof(filebrowser_dir));
        filebrowser_slot = -1;
      }
      is_filebrowser = false;
      need_redraw = true;
    }

  } else {

        // down
        if (key == KEY_DOWN) {
          curr_osd_item = find_next_item();
          need_redraw = true;
        }

        // up
        if (key == KEY_UP) {
          curr_osd_item = find_prev_item();
          need_redraw = true;
        }

        // right, enter
        if (key == KEY_RIGHT || key == KEY_ENTER) {
          if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILELOADER) {
            filebrowser_open();
          } else {
            core.osd[curr_osd_item].val++; 
            if (core.osd[curr_osd_item].val > core.osd[curr_osd_item].options_len-1) {
              core.osd[curr_osd_item].val = 0;
            }
            core_send(curr_osd_item);
            if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_SWITCH) {
              core.osd_need_save = true;
            }
          }
          need_redraw = true;
        }

        // left
        if (key == KEY_LEFT) {
          if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILELOADER) {
            filebrowser_open();
          } else {
            if (core.osd[curr_osd_item].val > 0) {
              core.osd[curr_osd_item].val--;
            } else {
              core.osd[curr_osd_item].val = core.osd[curr_osd_item].options_len-1;
            }
            core_send(curr_osd_item);
            if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_SWITCH) {
              core.osd_need_save = true;
            }
          }
          need_redraw = true;
        }

        // delete, backspace: eject the image of a file mounter item
        if (key == KEY_DELETE || key == KEY_BACKSPACE) {
          if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILEMOUNTER && file_slots[core.osd[curr_osd_item].slot_id].is_mounted) {
            app_core_eject(curr_osd_item);
            need_redraw = true;
          }
        }

        // s
        if (key == KEY_S) {
          osd_return_state = state_main;
          osd_state = state_setup;
        }

        // a
        if (key == KEY_A) {
          osd_return_state = state_main;
          osd_state = state_about;
        }
  }

  if (need_redraw) {
    need_redraw = false;
    // cls screen while changing screen mode
    if (prev_is_filebrowser != is_filebrowser) {
      prev_is_filebrowser = is_filebrowser;
      osd_fill(0, APP_COREBROWSER_MENU_OFFSET, 31, APP_COREBROWSER_MENU_OFFSET + 16, ' ');
    }
    if (is_filebrowser) {
      if (filebrowser_slot == -1 || filebrowser_slot != core.osd[curr_osd_item].slot_id) {
        filebrowser_slot = app_core_init_filebrowser();
      }
      if (filebrowser_slot != -1) {
        app_core_filebrowser(APP_COREBROWSER_MENU_OFFSET);
      } else {
        is_filebrowser = false;
        prev_is_filebrowser = false;
        app_core_overlay(false);
      }
    } else {
      app_core_overlay(false);
    }
  }

  if (core.osd_need_save) {
    app_core_save(curr_osd_item);
  }
  osd_update();
}

int app_core_init_filebrowser() {

  if (!has_sd) return -1;

  // working dir
  char *dir = file_slots[core.osd[curr_osd_item].slot_id].dir;
  kg_path_root(dir, sizeof(file_slots[0].dir));

  sd_opendir(&root1, dir);
  d_print("Open dir "); d_print(dir); d_println();

  sd_close(&file1);

  d_println("Read file list");
  fb_clear();

  // the eject entry (a mounted image of a file mounter), the physical floppy drives (an F item
  // of a board with the floppy connector), the root and the parent dir
  uint16_t n_special = 0;
  if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILEMOUNTER && file_slots[core.osd[curr_osd_item].slot_id].is_mounted) {
    if (fb_add_eject()) n_special++;
  }
  if (core_osd_can_phys(curr_osd_item)) {
    for (uint8_t d = 0; d < 2; d++) {
      if (fb_add_phys(d)) n_special++;
    }
  }
  fb_add_root();
  if (strlen(dir) > 1) fb_add_parent();

  uint16_t presel_id = 0;
  uint16_t after_id = 0; // the image of another slot (presel_after): the next file is preselected
  const char *core_f = file_slots[core.osd[curr_osd_item].slot_id].filename;
  FILINFO fi;
  while (sd_dir_is_open(&root1) && f_readdir(&root1, &fi) == FR_OK && fi.fname[0]) {
    uint16_t id;
    if (!sd_file_id(&fi, &id) || !fb_add_file(fi.fname, id)) continue;
    if (core_f[0] && strcasecmp(core_f, fi.fname) == 0) presel_id = id;
    if (presel_after[0] && strcasecmp(presel_after, fi.fname) == 0) after_id = id;
  }

  d_printf("Files count %u", fb_len()); d_println();

  fb_sort();

  // preselect file in browser after sorting; not the eject or physical drive entries (they sort
  // first) if the image is in another dir: enter must not eject by accident
  uint16_t len = fb_len();
  if (n_special > 0 && len > n_special) {
    fb_select(n_special);
  }
  int presel_phys = kg_phys_drive(core_f);
  for (uint16_t i=0; i<len; i++) {
    if (presel_phys >= 0 && fb_phys(i) == presel_phys) {
      fb_select(i);
    }
    if (presel_id != 0 && presel_id == fb_id(i)) {
      fb_select(i);
    }
    if (presel_id == 0 && after_id != 0 && after_id == fb_id(i)) {
      fb_select(i + 1 < len ? i + 1 : i);
    }
  }

  return core.osd[curr_osd_item].slot_id;
}

void app_core_filebrowser(uint8_t vpos) {
  fb_draw(vpos, file_slots[core.osd[curr_osd_item].slot_id].dir, true);
}

void app_core_on_select_file() {
  if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILELOADER) {

    core_ioctl_send(core.osd[curr_osd_item].slot_id, true);

  } else if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILEMOUNTER) {
    // open the image and send its size (eject first)
    img_mount(core.osd[curr_osd_item].slot_id);
  }
}
