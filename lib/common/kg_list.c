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

uint16_t kg_list_window(uint16_t top, uint16_t sel, uint16_t visible, uint16_t total) {
  if (visible == 0 || total <= visible) return 0;
  if (sel < top) top = sel;
  else if (sel >= top + visible) top = sel - visible + 1;
  return top > total - visible ? total - visible : top;
}

bool kg_list_thumb(uint16_t height, uint16_t top, uint16_t visible, uint16_t total, uint16_t *pos, uint16_t *len) {
  if (total <= visible || height < 2) return false;
  uint32_t n = ((uint32_t) height * visible + total / 2) / total;
  if (n < 1) n = 1;
  if (n > height / 2u) n = height / 2u; // a long thumb hid where the list was
  uint32_t last_top = total - visible;
  if (top > last_top) top = last_top; // the last page: the thumb at the bottom
  uint32_t room = height - n;
  uint32_t p;
  if (top == 0) p = 0;
  else if (top == last_top) p = room;
  else {
    // the cells between the ends for the positions between them
    p = (room * top + last_top / 2) / last_top;
    if (room >= 2 && p < 1) p = 1;
    if (room >= 2 && p > room - 1) p = room - 1;
  }
  *pos = (uint16_t) p;
  *len = (uint16_t) n;
  return true;
}

bool kg_list_page_thumb(uint16_t height, uint16_t page, uint16_t pages, uint16_t *pos, uint16_t *len) {
  if (pages < 2 || height < 2) return false;
  if (page < 1) page = 1;
  if (page > pages) page = pages;
  uint32_t n = height / pages;
  if (n < 1) n = 1;
  uint32_t room = height - n;
  *pos = (uint16_t) ((room * (page - 1) + (pages - 1) / 2) / (pages - 1));
  *len = (uint16_t) n;
  return true;
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
