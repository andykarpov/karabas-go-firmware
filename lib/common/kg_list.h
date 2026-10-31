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

// lists where only some items take the cursor (the options of a core menu, not its text rows)
typedef bool (*kg_list_active_fn)(uint16_t i, void *ctx);
// the first item active() takes; len if none
uint16_t kg_list_first_active(uint16_t len, kg_list_active_fn active, void *ctx);
// the next item active() takes from sel in direction d (1 down, -1 up), around the ends; sel if
// there is no other (also when sel itself is not active and there is none at all)
uint16_t kg_list_step_active(uint16_t sel, uint16_t len, int d, kg_list_active_fn active, void *ctx);

// the pages of a list of len items, page items a page: at least 1 (an empty list has one page)
uint16_t kg_list_pages(uint16_t len, uint16_t page);
// the page of item sel, from 1
uint16_t kg_list_page(uint16_t sel, uint16_t page);

// the first item of a window of visible rows that keeps sel in view, moved as little as possible
// from top, and within 0..total - visible
uint16_t kg_list_window(uint16_t top, uint16_t sel, uint16_t visible, uint16_t total);

// the thumb of a scrollbar of height cells for total items, visible of them shown from item top
// (a window, or the first item of a page): its first cell and its length, proportional to
// visible / total, at least 1 and less than height. At the top only with top 0, at the bottom
// only with the last items shown (when there are 3 or more positions for it). false (no
// scrollbar) if the items fit or height is under 2.
bool kg_list_thumb(uint16_t height, uint16_t top, uint16_t visible, uint16_t total, uint16_t *pos, uint16_t *len);

#ifdef __cplusplus
}
#endif
