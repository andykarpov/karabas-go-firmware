#include "main.h"
#include "debug_out.h"
#include "hardware/sync.h"

#include <FreeRTOS.h>
#include <task.h>
static TaskHandle_t owner_task = nullptr;

#define DEBUG_BUF_SIZE 2048 // bytes kept for non-owner writers between two drains

static uint8_t debug_buf[DEBUG_BUF_SIZE];
static volatile uint32_t debug_head = 0; // write position (producers, under the lock)
static volatile uint32_t debug_tail = 0; // read position (owner, under the lock)
static volatile uint32_t debug_dropped = 0;
static spin_lock_t *debug_lock = spin_lock_init(spin_lock_claim_unused(true));

void debug_set_owner_task(void *task) {
  owner_task = (TaskHandle_t) task;
}

static bool is_owner() {
  if (__get_current_exception() != 0) return false; // never write to Serial from an irq
  if (owner_task != nullptr) return xTaskGetCurrentTaskHandle() == owner_task;
  return get_core_num() == 0;
}

static size_t debug_buffer(const uint8_t *data, size_t len) {
  uint32_t save = spin_lock_blocking(debug_lock);
  for (size_t i = 0; i < len; i++) {
    if (debug_head - debug_tail >= DEBUG_BUF_SIZE) {
      debug_dropped += len - i;
      break;
    }
    debug_buf[debug_head % DEBUG_BUF_SIZE] = data[i];
    debug_head++;
  }
  spin_unlock(debug_lock, save);
  return len;
}

void debug_drain() {
  if (!is_owner()) return;
  uint8_t chunk[64];
  for (;;) {
    size_t n = 0;
    uint32_t save = spin_lock_blocking(debug_lock);
    while (n < sizeof(chunk) && debug_tail != debug_head) {
      chunk[n++] = debug_buf[debug_tail % DEBUG_BUF_SIZE];
      debug_tail++;
    }
    spin_unlock(debug_lock, save);
    if (n == 0) break;
    Serial.write(chunk, n);
  }
  static uint32_t reported_dropped = 0;
  if (debug_dropped != reported_dropped) {
    reported_dropped = debug_dropped;
    Serial.printf("[debug output buffer overflow, %lu bytes dropped]\r\n", (unsigned long) reported_dropped);
  }
}

class DebugOut : public Print {
public:
  size_t write(uint8_t c) override {
    return write(&c, 1);
  }
  size_t write(const uint8_t *data, size_t len) override {
    if (is_owner()) {
      debug_drain(); // keep the order with buffered output
      return Serial.write(data, len);
    }
    return debug_buffer(data, len);
  }
};

static DebugOut debug_out_instance;

Print &debug_out() {
  return debug_out_instance;
}

void debug_flush() {
  if (!is_owner()) return;
  debug_drain();
  Serial.flush();
}
