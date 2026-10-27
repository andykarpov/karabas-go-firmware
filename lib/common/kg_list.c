#include "kg_list.h"

bool kg_list_move(uint16_t *sel, uint16_t len, uint16_t page, kg_list_move_t move) {
  uint16_t old = *sel;
  if (len == 0) {
    *sel = 0;
    return old != 0;
  }
  uint16_t s = old < len ? old : len - 1;
  if (page == 0) page = 1;
  switch (move) {
    case KG_LIST_UP:        s = s > 0 ? s - 1 : len - 1; break;
    case KG_LIST_DOWN:      s = s + 1 < len ? s + 1 : 0; break;
    case KG_LIST_PAGE_UP:   s = s > page ? s - page : 0; break;
    case KG_LIST_PAGE_DOWN: s = len - 1 - s > page ? s + page : len - 1; break;
  }
  *sel = s;
  return s != old;
}

uint16_t kg_list_pages(uint16_t len, uint16_t page) {
  if (page == 0) page = 1;
  return len ? (len + page - 1) / page : 1;
}

uint16_t kg_list_page(uint16_t sel, uint16_t page) {
  if (page == 0) page = 1;
  return sel / page + 1;
}

uint16_t kg_list_first_active(uint16_t len, kg_list_active_fn active, void *ctx) {
  for (uint16_t i = 0; i < len; i++) if (active(i, ctx)) return i;
  return len;
}

uint16_t kg_list_step_active(uint16_t sel, uint16_t len, int d, kg_list_active_fn active, void *ctx) {
  if (len == 0) return sel;
  uint16_t i = sel < len ? sel : 0;
  for (uint16_t n = 0; n < len; n++) {
    i = d < 0 ? (i > 0 ? i - 1 : len - 1) : (i + 1 < len ? i + 1 : 0);
    if (i == sel) break;
    if (active(i, ctx)) return i;
  }
  return sel;
}
