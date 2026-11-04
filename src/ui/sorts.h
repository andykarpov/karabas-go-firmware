#pragma once

#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <strings.h>
#include "types.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// qsort comparators of the OSD lists, case-insensitive, without heap allocations. qsort is not
// stable: equal keys are ordered by a second one, so a list comes out the same every time.

// hash is the first SORT_HASH_LEN chars of the name and is not null-terminated; a dir has
// SORT_DIR_MARK and the first chars of its name. The entries without a file (id 0) go first,
// then the dirs, then the files.
static inline int file_sort_rank(const file_list_sort_item_t *e) {
  return e->file_id == 0 ? 0 : e->hash[0] == SORT_DIR_MARK ? 1 : 2;
}

static inline int file_sort_cmp(const void *pa, const void *pb) {
  const file_list_sort_item_t *a = (const file_list_sort_item_t *) pa, *b = (const file_list_sort_item_t *) pb;
  int c = file_sort_rank(a) - file_sort_rank(b);
  if (c) return c;
  c = strncasecmp(a->hash, b->hash, SORT_HASH_LEN);
  return c ? c : (int) a->file_id - (int) b->file_id;
}

// the header strings of a core are padded with spaces: compared without them, case-insensitive
static inline int core_str_cmp(const char *a, const char *b) {
  while (*a == ' ') a++;
  while (*b == ' ') b++;
  for (;;) {
    // the end of a string is where only spaces are left
    const char *ea = a, *eb = b;
    while (*ea == ' ') ea++;
    while (*eb == ' ') eb++;
    bool end_a = *ea == '\0', end_b = *eb == '\0';
    if (end_a || end_b) return end_a && end_b ? 0 : end_a ? -1 : 1;
    int c = tolower((unsigned char) *a) - tolower((unsigned char) *b);
    if (c) return c;
    a++; b++;
  }
}

// the boot core goes first, then by core_sort_e (equal keys: by the name, then by the file)
static uint8_t core_sort_mode_ = CORE_SORT_NAME; // of cores_sort(), for the comparator

static inline int core_sort_cmp(const void *pa, const void *pb) {
  const core_list_item_t *a = (const core_list_item_t *) pa, *b = (const core_list_item_t *) pb;
  int c = (a->type != CORE_TYPE_BOOT) - (b->type != CORE_TYPE_BOOT);
  if (c) return c;
  if (core_sort_mode_ == CORE_SORT_ORDER && a->order != b->order) return (int) a->order - (int) b->order;
  if (core_sort_mode_ != CORE_SORT_FILE && (c = core_str_cmp(a->name, b->name)) != 0) return c;
  return strcasecmp(a->filename, b->filename);
}

static inline void files_sort(file_list_sort_item_t *items, size_t n) {
  qsort(items, n, sizeof(items[0]), file_sort_cmp);
}

static inline void cores_sort(core_list_item_t *items, size_t n, uint8_t mode) {
  core_sort_mode_ = mode < CORE_SORT_COUNT ? mode : CORE_SORT_NAME;
  qsort(items, n, sizeof(items[0]), core_sort_cmp);
}

#ifdef __cplusplus
}
#endif
