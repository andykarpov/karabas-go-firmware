#include "main.h"

// File list state shared by the core browser, file loader and file mounter screens

file_list_sort_item_t files[SORT_FILES_MAX];
uint16_t files_len = 0;
uint16_t file_sel = 0;
uint16_t file_page_size = MAX_CORES_PER_PAGE;
uint16_t file_pages = 1;
uint16_t file_page = 1;
file_list_item_t cached_names[MAX_CORES_PER_PAGE];
uint16_t cached_file_from, cached_file_to;
