#pragma once

// A byte ring buffer over a caller's array (one writer, one reader in the same task; no locks).
// A full ring drops new bytes. Pure C, no Arduino dependencies.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t *buf;
  uint32_t size;
  uint32_t head, tail; // free running: head - tail bytes are in the ring
} kg_ring_t;

void kg_ring_init(kg_ring_t *r, uint8_t *buf, uint32_t size);
void kg_ring_clear(kg_ring_t *r);
uint32_t kg_ring_count(const kg_ring_t *r);
uint32_t kg_ring_free(const kg_ring_t *r);
// false (the byte dropped) if the ring is full
bool kg_ring_put(kg_ring_t *r, uint8_t c);
// the oldest byte, -1 if the ring is empty
int kg_ring_get(kg_ring_t *r);
int kg_ring_peek(const kg_ring_t *r);

#ifdef __cplusplus
}
#endif
