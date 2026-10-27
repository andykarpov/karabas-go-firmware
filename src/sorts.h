#pragma once

#include <Arduino.h>
#include <algorithm>
#include <tuple>
#include "types.h"
#include "config.h"
#include <strings.h>

// case-insensitive comparators without heap allocations (std::sort calls them O(n log n) times)

// hash is the first SORT_HASH_LEN chars of the name and is not null-terminated
inline bool operator<(const file_list_sort_item_t &a, const file_list_sort_item_t &b) {
  return strncasecmp(a.hash, b.hash, SORT_HASH_LEN) < 0;
}

inline bool operator<(const core_list_item_t &a, const core_list_item_t &b) {
  return a.order < b.order;
}

inline bool operator<(const file_list_item_t &a, const file_list_item_t &b) {
  return strcasecmp(a.name, b.name) < 0;
}
