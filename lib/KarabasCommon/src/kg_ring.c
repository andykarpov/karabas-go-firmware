#include "kg_ring.h"

void kg_ring_init(kg_ring_t *r, uint8_t *buf, uint32_t size) {
  r->buf = buf;
  r->size = size;
  r->head = r->tail = 0;
}

void kg_ring_clear(kg_ring_t *r) {
  r->tail = r->head;
}

uint32_t kg_ring_count(const kg_ring_t *r) {
  return r->head - r->tail;
}

uint32_t kg_ring_free(const kg_ring_t *r) {
  return r->size - kg_ring_count(r);
}

bool kg_ring_put(kg_ring_t *r, uint8_t c) {
  if (kg_ring_count(r) >= r->size) return false;
  r->buf[r->head % r->size] = c;
  r->head++;
  return true;
}

int kg_ring_peek(const kg_ring_t *r) {
  if (r->head == r->tail) return -1;
  return r->buf[r->tail % r->size];
}

int kg_ring_get(kg_ring_t *r) {
  int c = kg_ring_peek(r);
  if (c >= 0) r->tail++;
  return c;
}
