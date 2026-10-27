#include "main.h"
#include "diag/hang_diag.h"
#include "crc32.h"
#include "spi0_bus.h"
#include "hardware/spi.h"
#include <stdarg.h>
#include "sd_card.h"

// The card shares SPI0 with the link and the FT812, which use their own clocks (spi0_bus):
// sd_card on a shared bus, each transfer under the SPI bus lock at the card's clock.
// The pass-through of the FPGA works up to 12 MHz; at 24 MHz every read failed (Mini G,
// sd2test).

#define SD2_MHZ 12
#define SD2_BLOCK 8           // sectors in a multi-sector transfer of the test (4 KB, an MSC request)
#define SD2_TEST_SECTORS 1024 // read from sector 0 at each clock, in each mode (512 KB)
#define SD2_HANG_STEP 0xD2

static sd_card_t sd2;
static uint32_t sd2_hz;

static void bus_acquire(void *ctx) { (void) ctx; spi_bus_lock(); spi0_bus_clock(sd2_hz); }
static void bus_release(void *ctx) { (void) ctx; spi_bus_unlock(); }
static void bus_cs(void *ctx, bool selected) { (void) ctx; gpio_put(PIN_MCU_SD2_CS, !selected); }
static void bus_clock(void *ctx, uint32_t hz) { (void) ctx; sd2_hz = hz; }
static void bus_send(void *ctx, const uint8_t *buf, size_t n) { (void) ctx; spi0_bus_xfer(buf, NULL, n); }
static void bus_recv(void *ctx, uint8_t *buf, size_t n) { (void) ctx; spi0_bus_read(buf, n); }

static const sd_spi_bus_t sd2_bus = { bus_acquire, bus_release, bus_cs, bus_clock, bus_send, bus_recv, NULL, false };

static bool sd2_begin_mhz(uint8_t mhz) {
  return sd_card_begin(&sd2, &sd2_bus, mhz * 1000000UL);
}

bool sd2_begin() {
  return sd2_begin_mhz(SD2_MHZ);
}

uint32_t sd2_sector_count() {
  return sd_card_sectors(&sd2);
}

static bool read_once(uint32_t sector, uint8_t *dst, uint32_t count) {
  return sd_card_read(&sd2, sector, dst, count);
}

static bool write_once(uint32_t sector, const uint8_t *src, uint32_t count) {
  return sd_card_write(&sd2, sector, src, count);
}

// a failed transfer: logged, the card is set up again (begin() only reads its registers) and
// the transfer is tried once more
static bool recover(const char *op, uint32_t sector, uint32_t count) {
  d_printf("SD2: %s %lu+%lu failed, error 0x%02X data 0x%lX", op, (unsigned long) sector,
           (unsigned long) count, sd_card_error(&sd2), (unsigned long) sd_card_error_data(&sd2));
  d_println();
  if (sd2_begin()) return true;
  d_printf("SD2: init failed, error 0x%02X data 0x%lX", sd_card_error(&sd2), (unsigned long) sd_card_error_data(&sd2));
  d_println();
  return false;
}

bool sd2_read(uint32_t sector, uint8_t *dst, uint32_t count) {
  if (read_once(sector, dst, count)) return true;
  if (!recover("read", sector, count)) return false;
  bool ok = read_once(sector, dst, count);
  d_println(ok ? "SD2: read again: ok" : "SD2: read again: failed");
  return ok;
}

bool sd2_write(uint32_t sector, const uint8_t *src, uint32_t count) {
  if (write_once(sector, src, count)) return true;
  if (!recover("write", sector, count)) return false;
  bool ok = write_once(sector, src, count);
  d_println(ok ? "SD2: write again: ok" : "SD2: write again: failed");
  return ok;
}

void sd2_sync() {
  sd_card_sync(&sd2);
}

// ---- AT+KARABAS=sd2test ----

static const uint8_t test_mhz[] = { 4, 8, 12, 24 };

static sd2_out_t out_fn;

static void out_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void out_printf(const char *fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  out_fn(buf);
}

static void print_error() {
  out_printf(" (error 0x%02X, data 0x%lX)", sd_card_error(&sd2), (unsigned long) sd_card_error_data(&sd2));
}

// Reads SD2_TEST_SECTORS in transfers of n sectors. A link frame after each one switches SPI0
// back to the clock of the link. A failed read is counted, and the card is set up again.
static bool read_pass(uint8_t *buf, uint8_t mhz, uint8_t n, uint32_t ref_crc, uint32_t *crc) {
  uint32_t us = 0, errors = 0;
  (*crc) = KG_CRC32_INIT;
  for (uint32_t s = 0; s < SD2_TEST_SECTORS; s += n) {
    hang_step(SD2_HANG_STEP);
    uint32_t t = us_now();
    bool ok = read_once(s, buf, n);
    us += us_now() - t;
    if (!ok) {
      if (errors++ == 0) { out_printf(" x%u read of %lu failed", n, (unsigned long) s); print_error(); }
      if (!sd2_begin_mhz(mhz)) { out_fn(", init failed"); print_error(); return false; }
      continue;
    }
    (*crc) = kg_crc32_update((*crc), buf, n * 512);
    spi_send(CMD_NOP, 0, 0);
  }
  (*crc) = kg_crc32_final((*crc));
  if (errors) { out_printf(" %lu errors", (unsigned long) errors); return false; }
  out_printf(" x%u %lu KB/s", n, (unsigned long) ((uint64_t) SD2_TEST_SECTORS * 512 * 1000000 / 1024 / us));
  if (ref_crc && (*crc) != ref_crc) { out_printf(" (*crc) %08lX MISMATCH", (unsigned long) (*crc)); return false; }
  return true;
}

void sd2_test(sd2_out_t out) {
  out_fn = out;
  static uint8_t buf[SD2_BLOCK * 512];
  if (!sd2_begin_mhz(test_mhz[0])) {
    out_fn("+SD2:no card (or a boot core without SD2)"); print_error(); out_fn("\r\n");
    out_fn("ERROR\r\n");
    return;
  }
  uint32_t sectors = sd2_sector_count();
  uint8_t cid[16];
  bool cid_ok = sd_card_cid(&sd2, cid);
  static const char *types[] = { "?", "SD1", "SD2", "SDHC" };
  out_printf("+SD2:%s, %lu MB", types[sd2.type < 4 ? sd2.type : 0], (unsigned long) (sectors / 2048));
  if (cid_ok) {
    // CID: manufacturer, OEM (2 chars), product (5 chars), revision, serial number
    uint32_t psn = ((uint32_t) cid[9] << 24) | ((uint32_t) cid[10] << 16) | ((uint32_t) cid[11] << 8) | cid[12];
    out_printf(", mid %02X oid %.2s pnm %.5s psn %08lX", cid[0], (const char *) cid + 1, (const char *) cid + 3, (unsigned long) psn);
  }
  out_fn("\r\n");

  uint32_t ref_crc = 0;
  for (size_t k = 0; k < sizeof(test_mhz); k++) {
    uint8_t mhz = test_mhz[k];
    out_printf("+SD2:%u MHz", mhz);
    if (!sd2_begin_mhz(mhz)) { out_fn(" init failed"); print_error(); out_fn("\r\n"); break; }
    read_once(0, buf, 1);
    out_printf(" (%lu kHz):", (unsigned long) (spi_get_baudrate(spi0) / 1000));
    uint32_t crc1, crc8;
    bool ok = read_pass(buf, mhz, 1, ref_crc, &crc1);
    if (ok && !ref_crc) ref_crc = crc1;
    ok = read_pass(buf, mhz, SD2_BLOCK, ref_crc, &crc8) && ok;
    if (ok && !ref_crc) ref_crc = crc8;
    out_fn(ok ? " OK\r\n" : " FAIL\r\n");
  }
  out_printf("+SD2:crc of the first %u KB %08lX\r\n", SD2_TEST_SECTORS / 2, (unsigned long) ref_crc);
  out_fn("OK\r\n");
}
