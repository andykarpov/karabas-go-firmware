#pragma once

#include <stdlib.h>
#include <strings.h>
#include "types.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// qsort comparators of the OSD lists, case-insensitive, without heap allocations. qsort is not
// stable: equal keys are ordered by a second one, so a list comes out the same every time.

// hash is the first SORT_HASH_LEN chars of the name and is not null-terminated
static inline int file_sort_cmp(const void *pa, const void *pb) {
  const file_list_sort_item_t *a = (const file_list_sort_item_t *) pa, *b = (const file_list_sort_item_t *) pb;
  int c = strncasecmp(a->hash, b->hash, SORT_HASH_LEN);
  return c ? c : (int) a->file_id - (int) b->file_id;
}

static inline int core_sort_cmp(const void *pa, const void *pb) {
  const core_list_item_t *a = (const core_list_item_t *) pa, *b = (const core_list_item_t *) pb;
  if (a->order != b->order) return (int) a->order - (int) b->order;
  return strcasecmp(a->filename, b->filename);
}

static inline void files_sort(file_list_sort_item_t *items, size_t n) {
  qsort(items, n, sizeof(items[0]), file_sort_cmp);
}

static inline void cores_sort(core_list_item_t *items, size_t n) {
  qsort(items, n, sizeof(items[0]), core_sort_cmp);
}

#ifdef __cplusplus
}
#endif
