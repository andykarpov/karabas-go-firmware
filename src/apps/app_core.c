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
// the rows of the core menu on screen (as many as a page of the file browser), and the first
// item shown: a longer menu scrolls with the cursor
#define MENU_ROWS MAX_CORES_PER_PAGE
static uint8_t menu_top = 0;
static bool is_filebrowser = false;
static int filebrowser_slot = -1; // the slot of the file list read last, -1: read it again
static bool prev_is_filebrowser = false;
// the dir of the slot when the file browser was opened: browsing changes the dir of the slot,
// esc puts it back, so it stays the dir of the mounted image
static char filebrowser_dir[256];

// the file mounter slot of the image chosen last: a slot without an image opens its browser in
// that dir (a game on several disks: DF1 starts where the disk of DF0 is)
static int last_mount_slot = -1;
// the image of that slot: the browser of the empty slot preselects the file after it (disk 2)
static char presel_after[256];

// the file slot of the item under the cursor
static core_file_slot_t *cur_slot() {
  return &file_slots[core.osd[curr_osd_item].slot_id];
}

static void filebrowser_open() {
  uint8_t slot = core.osd[curr_osd_item].slot_id;
  core_file_slot_t *fs = cur_slot();
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

// The core menu is a tree: the items of a page (core.osd[].page, 0 the main menu) are shown, a page
// entry (M) opens its page, Esc goes back to the parent. vis[] holds the items of the page shown,
// in the order of core.osd (their index is the switch number of the core); the cursor
// (curr_osd_item) is an item index, a hotkey of an item on another page takes the menu there.
static uint8_t cur_page = 0;
static uint8_t vis[MAX_OSD_ITEMS];
static uint8_t vis_len = 0;
static uint8_t page_cursor[256]; // the item + 1 the cursor was on when a page was left

static void build_vis(void) {
  vis_len = 0;
  for (uint8_t i = 0; i < core.osd_len; i++) {
    if (core.osd[i].page == cur_page && core_osd_shown(i)) vis[vis_len++] = i;
  }
}

// the rows of the page that take the cursor (options, file items, page entries; not the text
// rows, the separators, the disabled items)
static bool item_active(uint16_t pos, void *ctx) {
  (void) ctx;
  return pos < vis_len && core_osd_selectable(vis[pos]) && core_osd_enabled(vis[pos]);
}

static int vis_pos(uint8_t item) {
  for (uint8_t p = 0; p < vis_len; p++) if (vis[p] == item) return p;
  return -1;
}

// the first item of the page that takes the cursor (vis built)
static uint8_t first_item() {
  uint16_t p = kg_list_first_active(vis_len, item_active, NULL);
  return p < vis_len ? vis[p] : (vis_len ? vis[0] : 0);
}

// the page of the cursor (a hotkey may have moved it), the items of the page, the cursor on one
// of them that takes it
static void menu_sync(void) {
  if (curr_osd_item < core.osd_len && core.osd[curr_osd_item].page != cur_page) {
    cur_page = core.osd[curr_osd_item].page;
    menu_top = 0;
  }
  build_vis();
  int p = vis_pos(curr_osd_item);
  if (p < 0 || !item_active(p, NULL)) curr_osd_item = first_item();
}

// to page: the cursor where it was on that page (else on its first item)
static void menu_open_page(uint8_t page) {
  page_cursor[cur_page] = curr_osd_item + 1;
  cur_page = page;
  menu_top = 0;
  build_vis();
  uint8_t c = page_cursor[page];
  int p = c ? vis_pos(c - 1) : -1;
  curr_osd_item = p >= 0 && item_active(p, NULL) ? c - 1 : first_item();
}

// back to the parent page, the cursor on the entry of the page left
static void menu_back(void) {
  int e = core_osd_page_entry(cur_page);
  page_cursor[cur_page] = curr_osd_item + 1;
  cur_page = e >= 0 ? core.osd[e].page : 0;
  menu_top = 0;
  build_vis();
  curr_osd_item = e >= 0 ? e : first_item();
}

// the rows of the menu: on a sub-page the first row is its title
static uint8_t menu_rows(void) { return cur_page ? MENU_ROWS - 1 : MENU_ROWS; }

// the window keeps the cursor in view; on the first (last) option it goes to the top (bottom), so
// the text rows before (after) it are seen too
static uint8_t menu_window() {
  uint8_t rows = menu_rows();
  int sel = vis_pos(curr_osd_item);
  uint16_t top = kg_list_window(menu_top, sel < 0 ? 0 : sel, rows, vis_len);
  if (vis_len > rows && sel >= 0) {
    uint16_t last = vis_len;
    for (uint16_t p = vis_len; p-- > 0;) if (item_active(p, NULL)) { last = p; break; }
    if (curr_osd_item == first_item()) top = 0;
    else if (sel == last) top = vis_len - rows;
  }
  return (uint8_t) top;
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
    cur_page = 0;
    memset(page_cursor, 0, sizeof(page_cursor));
    build_vis();
    curr_osd_item = first_item();
    menu_top = 0;
  }
  app_core_menu(APP_COREBROWSER_MENU_OFFSET);

  // footer
  if (!is_filebrowser) {
    osd_set_pos(0, UI_ROW_HINT_LINE);
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
    if (cur_page || core.type == CORE_TYPE_FILELOADER) {
      osd_print(" ");
      ui_hotkey("Esc", " back");
    }
  }
  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK); 
  osd_line(UI_ROW_HINT);
  osd_set_pos(0, UI_ROW_HINT + 1); osd_print("Press Menu+Esc to toggle OSD");
  osd_update();
}

// the value of a core menu item as shown: the option, the image of a file item
static void item_value(uint8_t i, char *out, size_t size) {
  const core_osd_t *item = &core.osd[i];
  if (core_osd_is_file(i)) {
    const core_file_slot_t *fs = &file_slots[item->slot_id];
    int phys = kg_phys_drive(fs->filename);
    if (fs->is_mounted && phys >= 0) snprintf(out, size, "Phys DF%d", phys);
    else if (fs->is_mounted) kg_short_name(fs->filename, out, size - 1); // long names: "Princ~.trd"
    else snprintf(out, size, "-NO IMAGE-");
  } else {
    snprintf(out, size, "%-10s", item->options[item->val].name);
  }
}

// the title of a sub-page: the names of the page entries from the main menu ("Video > Advanced")
static void page_title(uint8_t y) {
  int path[8];
  uint8_t n = 0;
  for (int e = core_osd_page_entry(cur_page); e >= 0 && n < 8; ) {
    path[n++] = e;
    uint8_t parent = core.osd[e].page;
    e = parent ? core_osd_page_entry(parent) : -1;
  }
  char text[OSD_SIZE_X + 1];
  size_t len = snprintf(text, sizeof(text), "%c ", OSD_CHAR_ARROW_LEFT);
  while (n-- > 0 && len < sizeof(text)) {
    len += snprintf(text + len, sizeof(text) - len, "%s%s", core.osd[path[n]].name, n ? " > " : "");
  }
  ui_row_ink(0, y, text, false, OSD_COLOR_CYAN_I);
}

// the rows of the core menu: a text row, a separator, or the name, the value (yellow, the cursor
// on it; a page entry: an arrow) and the hotkey; a disabled item in grey
void app_core_menu(uint8_t vpos) {
  menu_sync();
  if (cur_page) page_title(vpos++);
  uint8_t rows = menu_rows();
  menu_top = menu_window();
  uint8_t x_end = ui_list_col_end(rows, vis_len);
  uint8_t hotkey_w = x_end - 22 + 1; // 10 chars, 9 with the scrollbar
  for (uint8_t row = 0; row < rows; row++) {
    uint16_t p = menu_top + row;
    uint8_t y = vpos + row;
    char text[33];
    if (p >= vis_len) {
      ui_row_to(0, x_end, y, "", false, OSD_COLOR_WHITE);
      continue;
    }
    uint8_t i = vis[p];
    const core_osd_t *item = &core.osd[i];
    if (item->type == CORE_OSD_TYPE_TEXT) {
      snprintf(text, sizeof(text), "%s%s", item->name, item->hotkey);
      ui_row_to(0, x_end, y, text, false, OSD_COLOR_WHITE);
      continue;
    }
    if (item->type == CORE_OSD_TYPE_SEPARATOR) {
      osd_set_color(OSD_COLOR_GREY, OSD_COLOR_BLACK);
      osd_set_pos(0, y);
      for (uint8_t x = 0; x <= x_end; x++) osd_putc(OSD_CHAR_LINE);
      continue;
    }
    bool enabled = core_osd_enabled(i);
    snprintf(text, 11, "%s", item->name);
    ui_row_to(0, x_end, y, text, false, enabled ? OSD_COLOR_WHITE : OSD_COLOR_GREY);
    if (core_osd_selectable(i)) {
      if (item->type == CORE_OSD_TYPE_PAGE) snprintf(text, 11, "%c         ", OSD_CHAR_ARROW_RIGHT);
      else item_value(i, text, 11);
      bool sel = curr_osd_item == i;
      uint8_t ink = enabled ? OSD_COLOR_YELLOW_I : OSD_COLOR_GREY;
      osd_set_color(sel ? OSD_COLOR_BLACK : ink, sel ? ink : OSD_COLOR_BLACK);
      osd_set_pos(11, y); osd_print(text);
      osd_set_color(enabled ? OSD_COLOR_CYAN_I : OSD_COLOR_GREY, OSD_COLOR_BLACK);
      snprintf(text, hotkey_w + 1, "%s", item->hotkey);
      osd_set_pos(22, y); osd_print(text);
    } else {
      // a trigger: the hotkey where the value would be
      osd_set_color(enabled ? OSD_COLOR_CYAN_I : OSD_COLOR_GREY, OSD_COLOR_BLACK);
      snprintf(text, x_end - 11 + 2, "%s", item->hotkey);
      osd_set_pos(11, y); osd_print(text);
    }
  }
  ui_scrollbar(vpos, rows, menu_top, rows, vis_len);
  // no osd_update() here: app_core_overlay() draws the footer first, else the cleared footer
  // went out and came back on every move
}

void app_core_save(uint8_t pos)
{
    if (!sd_open(&file1, core.filename, FA_READ | FA_WRITE)) {
      d_println("Unable to open bitstream file to write");
      return;
    }
    core.osd_need_save = false;
    if (core_osd_is_file(pos)) {
      // save file mounter data (dir, filename) into the core
      uint32_t offset = file_slots[core.osd[pos].slot_id].offset_dir;
      file_seek(offset); 
      file_write_bytes(file_slots[core.osd[pos].slot_id].dir, 256);
      offset = file_slots[core.osd[pos].slot_id].offset_filename;
      file_seek(offset); 
      file_write_bytes(file_slots[core.osd[pos].slot_id].filename, 256);
    } else {
      // save switch state into the core
      core.osd[pos].prev_val = core.osd[pos].val;
      file_seek(FILE_POS_SWITCHES_DATA + pos);
      sd_write(&file1, &core.osd[pos].val, 1);
    }
    sd_close(&file1);
}

// the file browser in the dir of the slot, which has just changed (the image stays mounted
// until a file is chosen)
static void filebrowser_dir_changed() {
  d_print("Enter directory "); d_print(cur_slot()->dir); d_println();
  app_core_init_filebrowser();
  app_core_filebrowser(APP_COREBROWSER_MENU_OFFSET);
}

// Enter in the file browser: eject, a physical drive, the root, the parent, a dir or an image.
// true if it went into another dir (the browser is drawn already)
static bool filebrowser_enter() {
  uint16_t sel = fb_sel();
  if (fb_len() == 0) return false;
  sd_close(&file1);
  core_file_slot_t *fs = cur_slot();
  FILINFO fi;
  if (fb_is_eject(sel)) {
    app_core_eject(curr_osd_item);
    is_filebrowser = false;
  } else if (fb_phys(sel) >= 0) {
    app_core_select_phys(curr_osd_item, fb_phys(sel));
    is_filebrowser = false;
  } else if (fb_is_root(sel)) {
    strcpy(fs->dir, "/");
    filebrowser_dir_changed();
    return true;
  } else if (fb_is_parent(sel)) {
    kg_path_parent(fs->dir);
    kg_path_collapse(fs->dir);
    filebrowser_dir_changed();
    return true;
  } else if (sd_find_id(&root1, fb_id(sel), &fi)) {
    if (fi.fattrib & AM_DIR) {
      char path[KG_PATH_MAX];
      kg_path_join(path, sizeof(path), fs->dir, fi.fname);
      kg_path_collapse(path);
      snprintf(fs->dir, sizeof(fs->dir), "%s", path);
      filebrowser_dir_changed();
      return true;
    }
    // an image, if its extension matches
    d_printf("Selecting file id %d: %s", fb_id(sel), fi.fname); d_println();
    if (kg_name_has_ext(fi.fname, fs->ext)) {
      strcpy(fs->filename, fi.fname);
      fs->is_mounted = true;
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
  core.osd_need_save = true;
  return false;
}

void app_core_on_keyboard() {
  uint8_t key = ui_nav_key();

  if (is_filebrowser) {
    // up / down, left / right a page
    if (fb_key(key)) need_redraw = true;

    if (key == KEY_ENTER) {
      if (filebrowser_enter()) return; // another dir: drawn already
      need_redraw = true;
    }

    // esc: back to the dir of the mounted image
    if (key == KEY_ESC) {
      core_file_slot_t *fs = cur_slot();
      if (strcmp(fs->dir, filebrowser_dir) != 0) {
        memcpy(fs->dir, filebrowser_dir, sizeof(filebrowser_dir));
        filebrowser_slot = -1;
      }
      is_filebrowser = false;
      need_redraw = true;
    }
  } else {
    menu_sync();
    if (key == KEY_DOWN || key == KEY_UP) {
      int p = vis_pos(curr_osd_item);
      if (p >= 0) {
        p = kg_list_step_active(p, vis_len, key == KEY_DOWN ? 1 : -1, item_active, NULL);
        curr_osd_item = vis[p];
      }
      need_redraw = true;
    }

    // esc: back to the parent page; from the main page of a file loader core, back to its files
    if (key == KEY_ESC && cur_page) {
      menu_back();
      need_redraw = true;
    } else if (key == KEY_ESC && core.type == CORE_TYPE_FILELOADER) {
      osd_state = state_file_loader;
      return;
    }

    // right / enter, left: a page entry opens its page; the browser of a file item, else the next
    // / previous option
    if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_PAGE) {
      if (key == KEY_RIGHT || key == KEY_ENTER) {
        menu_open_page(core.osd[curr_osd_item].open_page);
        need_redraw = true;
      }
    } else if (!core_osd_enabled(curr_osd_item)) {
      // a page with no item to change
    } else if (key == KEY_RIGHT || key == KEY_ENTER || key == KEY_LEFT) {
      if (core_osd_is_file(curr_osd_item)) {
        filebrowser_open();
      } else {
        core_osd_change(curr_osd_item, key == KEY_LEFT ? -1 : 1);
      }
      need_redraw = true;
    }

    // delete, backspace: eject the image of a file mounter item
    if ((key == KEY_DELETE || key == KEY_BACKSPACE) &&
        core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILEMOUNTER && cur_slot()->is_mounted) {
      app_core_eject(curr_osd_item);
      need_redraw = true;
    }

    if (key == KEY_S || key == KEY_A) {
      osd_return_state = state_main;
      osd_state = key == KEY_S ? state_setup : state_about;
    }
  }

  if (need_redraw) {
    need_redraw = false;
    // cls screen while changing screen mode
    if (prev_is_filebrowser != is_filebrowser) {
      prev_is_filebrowser = is_filebrowser;
      osd_fill(0, APP_COREBROWSER_MENU_OFFSET, UI_COL_LAST, APP_COREBROWSER_MENU_OFFSET + 16, ' ');
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
  char *dir = cur_slot()->dir;
  kg_path_root(dir, sizeof(file_slots[0].dir));

  sd_opendir(&root1, dir);
  d_print("Open dir "); d_print(dir); d_println();

  sd_close(&file1);

  d_println("Read file list");
  fb_clear();

  // the dirs and the files with the extensions of the slot; the eject entry (a mounted image of a file mounter), the physical floppy drives (an F item
  // of a board with the floppy connector), the root and the parent dir
  uint16_t n_special = 0;
  if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILEMOUNTER && cur_slot()->is_mounted) {
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
  const char *core_f = cur_slot()->filename;
  const char *exts = cur_slot()->ext;
  FILINFO fi;
  while (sd_dir_is_open(&root1) && f_readdir(&root1, &fi) == FR_OK && fi.fname[0]) {
    uint16_t id;
    if (!(fi.fattrib & AM_DIR) && !kg_name_has_ext(fi.fname, exts)) continue;
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

// the browser, and the extensions of the slot on the last hint row (the toggle hint is back
// with the menu)
void app_core_filebrowser(uint8_t vpos) {
  char b[OSD_SIZE_X + 1] = "Files: ";
  kg_exts_text(cur_slot()->ext, b + 7, OSD_SIZE_X - 7);
  ui_row(0, UI_ROW_HINT + 1, b[7] ? b : "Files: all", false);
  fb_draw(vpos, cur_slot()->dir, true); // sends the OSD
}

void app_core_on_select_file() {
  if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILELOADER) {

    core_ioctl_send(core.osd[curr_osd_item].slot_id, true);

  } else if (core.osd[curr_osd_item].type == CORE_OSD_TYPE_FILEMOUNTER) {
    // open the image and send its size (eject first)
    img_mount(core.osd[curr_osd_item].slot_id);
  }
}
