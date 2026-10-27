#include "main.h"
#include "debug_out.h"
#include "hardware/sync.h"
#include "tusb.h"

#include "pico/mutex.h"
#include <FreeRTOS.h>
#include <task.h>

// taken by the USBD task while it runs tud_task(): see src/usb/usb_device.cpp
extern mutex_t __usb_mutex;
static TaskHandle_t owner_task = nullptr;

// all output, until the owner sends it (as much as the CDC fifo takes), and for everyone until
// a terminal opens the port: after a reboot the host needs a moment to bring the CDC port back, and the
// boot log (usb enumeration included) would be lost otherwise
#define DEBUG_BUF_SIZE 8192

static uint8_t debug_buf[DEBUG_BUF_SIZE];
static volatile uint32_t debug_head = 0; // write position (producers, under the lock)
static volatile uint32_t debug_tail = 0; // read position (owner, under the lock)
static volatile uint32_t debug_dropped = 0;
static spin_lock_t *debug_lock = spin_lock_init(spin_lock_claim_unused(true));

void debug_set_owner_task(void *task) {
  owner_task = (TaskHandle_t) task;
}

// a terminal has the port open. Not Serial's operator bool: it yields when nothing is connected,
// which would hand the APP loop to the OLED task on every pass
static bool port_open() {
  return tud_cdc_connected();
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

// as much as the CDC fifo takes now: never waits. Adafruit's Serial.write loops with yield()
// until all of it is sent, and the board hung for good at a boot right after a flash, with a
// terminal opening the port while the boot log was written
void debug_drain() {
  if (!is_owner() || !port_open()) return;
  // a write claims the endpoint with TinyUSB's (per core) mutex: USBD, above APP, would wait
  // for it forever. Taken already: USBD is in tud_task(), the rest goes out later
  if (!mutex_try_enter(&__usb_mutex, NULL)) return;
  uint8_t chunk[64];
  bool sent = false;
  for (;;) {
    size_t room = tud_cdc_write_available();
    if (room > sizeof(chunk)) room = sizeof(chunk);
    size_t n = 0;
    uint32_t save = spin_lock_blocking(debug_lock);
    while (n < room && debug_tail != debug_head) {
      chunk[n++] = debug_buf[debug_tail % DEBUG_BUF_SIZE];
      debug_tail++;
    }
    spin_unlock(debug_lock, save);
    if (n == 0) break;
    tud_cdc_write(chunk, n);
    sent = true;
  }
  if (sent) tud_cdc_write_flush();
  mutex_exit(&__usb_mutex);
  static uint32_t reported_dropped = 0;
  if (debug_dropped != reported_dropped) {
    reported_dropped = debug_dropped;
    char b[64];
    int len = snprintf(b, sizeof(b), "[debug output buffer overflow, %lu bytes dropped]\r\n", (unsigned long) reported_dropped);
    debug_buffer((const uint8_t *) b, len); // goes out with the next drain
  }
}

void debug_discard() {
  uint32_t save = spin_lock_blocking(debug_lock);
  debug_tail = debug_head;
  spin_unlock(debug_lock, save);
}

// everything goes through the buffer; the owner sends what fits right away
void debug_write(const char *data, size_t len) {
  debug_buffer((const uint8_t *) data, len);
  debug_drain();
}

void debug_print(const char *s) {
  if (s) debug_write(s, strlen(s));
}

void debug_println(const char *s, ...) {
  debug_print(s);
  debug_write("\r\n", 2);
}

// a small buffer on the stack (any task may log), a longer line from the heap (the DIAG line)
void debug_vprintf(const char *fmt, va_list ap) {
  char buf[128];
  va_list ap2;
  va_copy(ap2, ap);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  if (n >= 0 && (size_t) n < sizeof(buf)) {
    debug_write(buf, n);
  } else if (n > 0) {
    char *big = (char *) malloc(n + 1);
    if (big) {
      vsnprintf(big, n + 1, fmt, ap2);
      debug_write(big, n);
      free(big);
    }
  }
  va_end(ap2);
}

void debug_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  debug_vprintf(fmt, ap);
  va_end(ap);
}

void debug_flush() {
  debug_drain();
}
