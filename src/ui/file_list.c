#include "main.h"

// File list state shared by the core browser, file loader and file mounter screens

// Sorted directory index of the file loader / file mounter browsers. It grows in chunks up to
// SORT_FILES_MAX entries, instead of a static 48 KB array that was allocated even when no
// browser was ever opened.
#define FILES_CHUNK 256 // entries (1.5 KB)

file_list_sort_item_t *files = NULL;
uint16_t files_len = 0;
static uint16_t files_cap = 0;

void files_clear() {
  files_len = 0;
  // give a large buffer back; a small one is kept for the next list
  if (files_cap > FILES_CHUNK) {
    free(files);
    files = NULL;
    files_cap = 0;
  }
}

file_list_sort_item_t *files_append() {
  if (files_len >= SORT_FILES_MAX) return NULL;
  if (files_len >= files_cap) {
    uint32_t cap = (uint32_t) files_cap + FILES_CHUNK;
    if (cap > SORT_FILES_MAX) cap = SORT_FILES_MAX;
    file_list_sort_item_t *p = (file_list_sort_item_t *) realloc(files, cap * sizeof(file_list_sort_item_t));
    if (p == NULL) {
      d_printf("File list: out of memory at %u entries", files_len); d_println();
      return NULL;
    }
    files = p;
    files_cap = cap;
  }
  file_list_sort_item_t *item = &files[files_len++];
  memset(item, 0, sizeof(*item));
  return item;
}

uint16_t file_sel = 0;
uint16_t file_page_size = MAX_CORES_PER_PAGE;
uint16_t file_pages = 1;
uint16_t file_page = 1;
file_list_item_t cached_names[MAX_CORES_PER_PAGE];
uint16_t cached_file_from, cached_file_to;
