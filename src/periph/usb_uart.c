#include "usb_uart.h"
#include "tusb.h"
#include "hardware/sync.h"
#include "input/usb_joy.h"
#include "config.h"
#include "diag/debug_out.h"
#include "storage/settings.h"

// Two rings between APP and the USB task, under usb_host_lock() (no hardware spinlock of
// their own). Every TinyUSB call is made in the USB task: the host stack runs on core 1.
#define RX_SIZE 2048 // adapter -> core: the core takes the bytes at the line rate
#define TX_SIZE 1024 // core -> adapter
#define NO_ADAPTER 0xff

static uint8_t rx_ring[RX_SIZE], tx_ring[TX_SIZE];
static uint16_t rx_head, rx_used, tx_head;
static volatile uint16_t tx_used;          // read without the lock by the idle check
static volatile uint8_t cdc_idx = NO_ADAPTER; // the cdc_host index of the adapter in use
static volatile uint32_t want_baud = 115200; // set by APP
static uint32_t cur_baud;                    // USB task: the adapter's speed
static bool baud_busy;                       // USB task: a SET_LINE_CODING is on its way
static uint32_t rx_count, tx_count, rx_lost, tx_lost;

static void flush_rings(void) { // under the lock
  rx_head = rx_used = tx_head = 0;
  tx_used = 0;
}

// ---- APP side ----

bool usb_uart_active(void) {
  return cdc_idx != NO_ADAPTER;
}

void usb_uart_write(uint8_t data) {
  uint32_t saved = spin_lock_blocking(usb_host_lock());
  if (cdc_idx == NO_ADAPTER) {
    // unplugged meanwhile
  } else if (tx_used < TX_SIZE) {
    tx_ring[(tx_head + tx_used) % TX_SIZE] = data;
    tx_used++;
  } else {
    tx_lost++;
  }
  spin_unlock(usb_host_lock(), saved);
}

int usb_uart_read(void) {
  int c = -1;
  uint32_t saved = spin_lock_blocking(usb_host_lock());
  if (rx_used) {
    c = rx_ring[rx_head];
    rx_head = (rx_head + 1) % RX_SIZE;
    rx_used--;
  }
  spin_unlock(usb_host_lock(), saved);
  return c;
}

void usb_uart_set_baud(uint32_t baud) {
  if (baud) want_baud = baud;
}

void usb_uart_stats(usb_uart_stats_t *stats) {
  uint32_t saved = spin_lock_blocking(usb_host_lock());
  stats->mounted = cdc_idx != NO_ADAPTER;
  stats->baud = cur_baud;
  stats->rx = rx_count;
  stats->tx = tx_count;
  stats->rx_lost = rx_lost;
  stats->tx_lost = tx_lost;
  spin_unlock(usb_host_lock(), saved);
}

// ---- USB task side ----

void tuh_cdc_mount_cb(uint8_t idx) {
  tuh_itf_info_t info;
  tuh_cdc_itf_get_info(idx, &info);
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(info.daddr, &vid, &pid);
  if (cdc_idx != NO_ADAPTER) {
    d_printf("USB UART %04x:%04x address %u: another adapter is in use, ignored", vid, pid, info.daddr); d_println();
    return;
  }
  uint32_t saved = spin_lock_blocking(usb_host_lock());
  flush_rings();
  cdc_idx = idx;
  spin_unlock(usb_host_lock(), saved);
  cur_baud = 115200; // CFG_TUH_CDC_LINE_CODING_ON_ENUM
  baud_busy = false;
  d_printf("USB UART mounted %04x:%04x address %u: the core serial goes to it", vid, pid, info.daddr); d_println();
}

void tuh_cdc_umount_cb(uint8_t idx) {
  if (idx != cdc_idx) return;
  uint32_t saved = spin_lock_blocking(usb_host_lock());
  cdc_idx = NO_ADAPTER;
  flush_rings();
  spin_unlock(usb_host_lock(), saved);
  d_printf("USB UART disconnected: rx %lu tx %lu lost rx %lu tx %lu", (unsigned long) rx_count,
           (unsigned long) tx_count, (unsigned long) rx_lost, (unsigned long) tx_lost); d_println();
}

void tuh_cdc_rx_cb(uint8_t idx) {
  if (idx != cdc_idx) { tuh_cdc_read_clear(idx); return; }
  uint8_t buf[64];
  uint32_t n;
  while ((n = tuh_cdc_read(idx, buf, sizeof(buf))) > 0) {
    uint32_t saved = spin_lock_blocking(usb_host_lock());
    for (uint32_t i = 0; i < n; i++) {
      if (rx_used < RX_SIZE) {
        rx_ring[(rx_head + rx_used) % RX_SIZE] = buf[i];
        rx_used++;
        rx_count++;
      } else {
        rx_lost++;
      }
    }
    spin_unlock(usb_host_lock(), saved);
  }
}

static void baud_done(tuh_xfer_t *xfer) {
  if (xfer->result != XFER_RESULT_SUCCESS) {
    d_printf("USB UART: %lu baud not set", (unsigned long) xfer->user_data); d_println();
  }
  cur_baud = (uint32_t) xfer->user_data; // not tried again on a failure
  baud_busy = false;
}

void __not_in_flash_func(usb_uart_host_tick)(void) { // the USB loop runs from RAM
  const uint8_t idx = cdc_idx;
  if (idx == NO_ADAPTER) return;
  const uint32_t baud = want_baud;
  if (baud != cur_baud && !baud_busy) {
    // false while the control pipe is busy: tried again on the next turn
    if (tuh_cdc_set_baudrate(idx, baud, baud_done, baud)) baud_busy = true;
  }
  if (!tx_used) return;
  uint8_t buf[64];
  uint32_t n = tuh_cdc_write_available(idx);
  if (n > sizeof(buf)) n = sizeof(buf);
  uint32_t saved = spin_lock_blocking(usb_host_lock());
  if (n > tx_used) n = tx_used;
  for (uint32_t i = 0; i < n; i++) buf[i] = tx_ring[(tx_head + i) % TX_SIZE];
  tx_head = (tx_head + n) % TX_SIZE;
  tx_used -= n;
  tx_count += n;
  spin_unlock(usb_host_lock(), saved);
  if (n) {
    tuh_cdc_write(idx, buf, n);
    tuh_cdc_write_flush(idx);
  }
}
