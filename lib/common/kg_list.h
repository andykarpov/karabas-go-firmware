#pragma once

// The cursor of an OSD list: Up / Down move by one around the ends, Left / Right by a page and
// stop at the ends. Pure C.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  KG_LIST_UP,
  KG_LIST_DOWN,
  KG_LIST_PAGE_UP,
  KG_LIST_PAGE_DOWN,
} kg_list_move_t;

// moves *sel in a list of len items, page items a page; true if it moved. An empty list keeps
// *sel at 0, a selection past the end is put on the last item first.
bool kg_list_move(uint16_t *sel, uint16_t len, uint16_t page, kg_list_move_t move);

// the pages of a list of len items, page items a page: at least 1 (an empty list has one page)
uint16_t kg_list_pages(uint16_t len, uint16_t page);
// the page of item sel, from 1
uint16_t kg_list_page(uint16_t sel, uint16_t page);

#ifdef __cplusplus
}
#endif
