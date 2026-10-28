#include "main.h"
#include "usb_diag.h"
#include <stdbool.h>
#include <stdint.h>
#include "pio_usb.h"
#include "tusb.h"
#include "hardware/structs/iobank0.h"

static const char *const line_names[4] = {"SE0", "FS idle", "LS idle", "SE1"};

// who drives D+ / D- (DPDM pinout: D- is D+ + 1): "pio1 out 0" when a PIO state machine drives
// the pin low (the host's bus reset, or a stuck transmit), "in 0" when nothing on our side does
static void pin_state(char *buf, size_t size, uint pin) {
  uint32_t st = io_bank0_hw->io[pin].status;
  bool oe = st & IO_BANK0_GPIO0_STATUS_OETOPAD_BITS;
  bool out = st & IO_BANK0_GPIO0_STATUS_OUTTOPAD_BITS;
  bool in = st & IO_BANK0_GPIO0_STATUS_INFROMPAD_BITS;
  uint func = gpio_get_function(pin);
  char fn[8];
  if (func == GPIO_FUNC_PIO0) strcpy(fn, "pio0");
  else if (func == GPIO_FUNC_PIO1) strcpy(fn, "pio1");
  else if (func == GPIO_FUNC_SIO) strcpy(fn, "sio");
  else snprintf(fn, sizeof(fn), "fn%u", func);
  if (oe) snprintf(buf, size, "%s out %d", fn, out);
  else snprintf(buf, size, "%s in %d", fn, in);
}

static void pins_state(char *buf, size_t size) {
  char dp[16], dm[16];
  pin_state(dp, sizeof(dp), HOST_PIN_DP);
  pin_state(dm, sizeof(dm), HOST_PIN_DP + 1);
  snprintf(buf, size, "D+ %s, D- %s", dp, dm);
}

// root port events, as the SOF irq counted them. Connects are only counted: a log line from
// core1 at boot made the FT812 picture of the Mini G start black 4 times out of 7 (not found why)
void __not_in_flash_func(usb_diag_tick)() { // the USB loop runs from RAM
  static uint32_t connects = 0, disconnects = 0, overflows = 0;
  uint32_t c = pio_usb_diag.connects, d = pio_usb_diag.disconnects, o = pio_usb_diag.rx_overflows;
  if (c == connects && d == disconnects && o == overflows) return;
  uint8_t root = pio_usb_host_root_state();
  if (d != disconnects) {
    char pins[48];
    pins_state(pins, sizeof(pins));
    // own: our tx held SE0 (stopped in its EOP), the lines were let go for a reconnect
    d_printf("USB root: disconnect (%lu in all, %lu own), line %s, %s\r\n", (unsigned long) d,
      (unsigned long) pio_usb_diag.own_se0, line_names[(root >> 4) & 3], pins);
  }
  if (o != overflows) {
    d_printf("USB root: %lu packets longer than the rx buffer (%lu in all)\r\n", (unsigned long) (o - overflows), (unsigned long) o);
  }
  connects = c; disconnects = d; overflows = o;
}

void usb_diag_print() {
  static uint32_t last_ms = 0, last_frame = 0;
  uint32_t now = ms_now(), frame = pio_usb_host_get_frame_number();
  uint32_t sof = last_ms ? (frame - last_frame) * 1000ULL / (now - last_ms) : 0;
  last_ms = now; last_frame = frame;
  uint8_t root = pio_usb_host_root_state();
  uint32_t sof_gap = pio_usb_diag.max_sof_gap_us, frame_us = pio_usb_diag.max_frame_us;
  pio_usb_diag.max_sof_gap_us = 0;
  pio_usb_diag.max_frame_us = 0;
  char pins[48];
  pins_state(pins, sizeof(pins));
  d_printf(" | usb sof %lu/s, max gap %lu us, max frame %lu us, root %s%s%s line %s (%s), conn %lu disc %lu own %lu ovf %lu",
    (unsigned long) sof, (unsigned long) sof_gap, (unsigned long) frame_us,
    (root & 1) ? "" : "uninit ", (root & 2) ? "connected" : "empty", (root & 4) ? " suspended" : "",
    line_names[(root >> 4) & 3], pins,
    (unsigned long) pio_usb_diag.connects, (unsigned long) pio_usb_diag.disconnects,
    (unsigned long) pio_usb_diag.own_se0,
    (unsigned long) pio_usb_diag.rx_overflows);
}
