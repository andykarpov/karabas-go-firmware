#include "main.h"
#include "ui/file_browser.h"
#include "ui/sorts.h"
#include "ui/ui_draw.h"
#include "kg_list.h"
#include "file_name.h"

// The sorted index grows in chunks up to SORT_FILES_MAX entries, instead of a static 48 KB array
// that was allocated even when no browser was ever opened.
#define FILES_CHUNK 256 // entries (1.5 KB)
#define PAGE MAX_CORES_PER_PAGE

static file_list_sort_item_t *files = NULL;
static uint16_t len = 0, cap = 0;
static uint16_t sel = 0;

// the names of the page on the screen, read once per page
static file_list_item_t names[PAGE];
static uint16_t names_from = 0;
static bool names_valid = false;

// the hashes of the entries without a file: '*' and '+' are not in FAT names and sort before "."
#define HASH_EJECT '*'
#define HASH_PHYS '+'

void fb_clear(void) {
  len = 0;
  sel = 0;
  names_valid = false;
  // give a large buffer back; a small one is kept for the next list
  if (cap > FILES_CHUNK) {
    free(files);
    files = NULL;
    cap = 0;
  }
}

static file_list_sort_item_t *append(void) {
  if (len >= SORT_FILES_MAX) return NULL;
  if (len >= cap) {
    uint32_t c = (uint32_t) cap + FILES_CHUNK;
    if (c > SORT_FILES_MAX) c = SORT_FILES_MAX;
    file_list_sort_item_t *p = (file_list_sort_item_t *) realloc(files, c * sizeof(file_list_sort_item_t));
    if (p == NULL) {
      d_printf("File list: out of memory at %u entries", len); d_println();
      return NULL;
    }
    files = p;
    cap = c;
  }
  file_list_sort_item_t *item = &files[len++];
  memset(item, 0, sizeof(*item));
  return item;
}

bool fb_add_file(const char *name, uint16_t id) {
  file_list_sort_item_t *item = append();
  if (!item) return false;
  memcpy(item->hash, name, SORT_HASH_LEN); // not terminated: the first chars of the name
  item->file_id = id;
  return true;
}

// an entry without a file (id 0), hash of up to 2 chars
static bool add_entry(char c0, char c1) {
  file_list_sort_item_t *item = append();
  if (!item) return false;
  item->hash[0] = c0;
  item->hash[1] = c1;
  return true;
}

bool fb_add_eject(void) { return add_entry(HASH_EJECT, 0); }
bool fb_add_phys(uint8_t drive) { return add_entry(HASH_PHYS, '0' + drive); }
bool fb_add_root(void) { return add_entry('.', 0); }
bool fb_add_parent(void) { return add_entry('.', '.'); }

void fb_sort(void) {
  files_sort(files, len);
  names_valid = false;
}

uint16_t fb_len(void) { return len; }
uint16_t fb_sel(void) { return sel; }
void fb_select(uint16_t i) { sel = i < len ? i : 0; }
uint16_t fb_id(uint16_t i) { return i < len ? files[i].file_id : 0; }

static bool entry_is(uint16_t i, char c0, char c1) {
  return i < len && files[i].file_id == 0 && files[i].hash[0] == c0 && files[i].hash[1] == c1;
}

bool fb_is_eject(uint16_t i) { return entry_is(i, HASH_EJECT, 0); }
int fb_phys(uint16_t i) {
  return i < len && files[i].file_id == 0 && files[i].hash[0] == HASH_PHYS ? files[i].hash[1] - '0' : -1;
}
bool fb_is_root(uint16_t i) { return entry_is(i, '.', 0); }
bool fb_is_parent(uint16_t i) { return entry_is(i, '.', '.'); }

bool fb_key(uint8_t key) {
  return ui_list_key(&sel, len, PAGE, key);
}

// the name of entry i as the browser shows it; root1 is the dir of the list
static void entry_name(uint16_t i, char *name, size_t size) {
  FILINFO fi;
  name[0] = '\0';
  if (fb_is_eject(i)) {
    snprintf(name, size, "[ Eject ]");
  } else if (fb_phys(i) >= 0) {
    snprintf(name, size, "[ Physical DF%d ]", fb_phys(i));
  } else if (fb_is_root(i)) {
    snprintf(name, size, ".");
  } else if (fb_is_parent(i)) {
    snprintf(name, size, "..");
  } else if (sd_find_id(&root1, files[i].file_id, &fi)) {
    kg_trim(fi.fname);
    snprintf(name, size, "%s", fi.fname);
  }
}

void fb_draw(uint8_t top, const char *dir, bool show_dir) {
  uint16_t page = kg_list_page(sel, PAGE);
  uint16_t from = (page - 1) * PAGE;
  bool cached = names_valid && names_from == from;
  if (!cached) sd_opendir(&root1, dir);

  for (uint16_t row = 0; row < PAGE; row++) {
    uint16_t i = from + row;
    if (!cached) {
      names[row].name[0] = '\0';
      if (i < len) entry_name(i, names[row].name, sizeof(names[row].name));
    }
    ui_row(0, top + row, names[row].name, i == sel && i < len);
  }
  names_from = from;
  names_valid = true;

  osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
  osd_line(top + PAGE);
  uint8_t y = top + PAGE + 1;
  char b[32];
  if (show_dir) {
    snprintf(b, sizeof(b), "Dir %.20s", dir);
    ui_row(0, y, "", false);
    osd_set_pos(8, y); osd_print(b);
    y++;
  }
  snprintf(b, sizeof(b), "Page %03u of %03u", page, kg_list_pages(len, PAGE));
  ui_row(0, y, "", false);
  osd_set_pos(8, y); osd_print(b);
  osd_update();
}
