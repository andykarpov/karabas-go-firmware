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
