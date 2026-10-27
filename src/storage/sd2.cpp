#include "main.h"
#include "diag/hang_diag.h"
#include "crc32.h"
#include "spi0_bus.h"

// The card shares SPI0 with the link and the FT812, which use their own clocks (spi0_bus).
// SdFat still goes through Arduino's SPI, which keeps its own copy of the clock: every access
// sets the card's clock with spi0_bus first (so that copy is right) and makes spi0_bus forget
// its clock after (SdFat may have changed it, the next user sets its own).
// USER_SPI_BEGIN: SdFat must not call SPI.begin() (the link set up SPI0), and sd2.end() must
// never be called (SPI.end() would stop the link).
// The pass-through of the FPGA works up to 12 MHz (SPI0 runs at 48 MHz / an even divider: a
// 16 MHz request gives 12 too); at 24 MHz every read failed (Mini G, sd2test).

#define SD2_MHZ 12
#define SD2_BLOCK 8           // sectors in a multi-sector transfer of the test (4 KB, an MSC request)
#define SD2_TEST_SECTORS 1024 // read from sector 0 at each clock, in each mode (512 KB)
#define SD2_HANG_STEP 0xD2

static SdSpiCard sd2;
static uint32_t sd2_hz = SD2_MHZ * 1000000UL;

// the bus for one SdFat call: locked, at the card's clock
struct Sd2Bus {
  SpiBusGuard lock;
  Sd2Bus() { spi0_bus_clock(sd2_hz); }
  ~Sd2Bus() { spi0_bus_forget_clock(); }
};

static bool sd2_begin_mhz(uint8_t mhz) {
  sd2_hz = mhz * 1000000UL;
  Sd2Bus g;
  return sd2.begin(SdSpiConfig(PIN_MCU_SD2_CS, SHARED_SPI | USER_SPI_BEGIN, SD_SCK_MHZ(mhz), &SPI));
}

bool sd2_begin() {
  return sd2_begin_mhz(SD2_MHZ);
}

uint32_t sd2_sector_count() {
  Sd2Bus g;
  return sd2.sectorCount();
}

static bool read_once(uint32_t sector, uint8_t *dst, uint32_t count) {
  Sd2Bus g;
  return count == 1 ? sd2.readSector(sector, dst) : sd2.readSectors(sector, dst, count);
}

static bool write_once(uint32_t sector, const uint8_t *src, uint32_t count) {
  Sd2Bus g;
  return count == 1 ? sd2.writeSector(sector, src) : sd2.writeSectors(sector, src, count);
}

// a failed transfer: logged, the card is set up again (begin() only reads its registers) and
// the transfer is tried once more
static bool recover(const char *op, uint32_t sector, uint32_t count) {
  d_printf("SD2: %s %lu+%lu failed, error 0x%02X data 0x%lX", op, (unsigned long) sector,
           (unsigned long) count, sd2.errorCode(), (unsigned long) sd2.errorData());
  d_println();
  if (sd2_begin()) return true;
  d_printf("SD2: init failed, error 0x%02X data 0x%lX", sd2.errorCode(), (unsigned long) sd2.errorData());
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
  Sd2Bus g;
  sd2.syncDevice();
}

// ---- AT+KARABAS=sd2test ----

static const uint8_t test_mhz[] = { 4, 8, 12, 24 };

static void print_error(Print &out) {
  out.printf(" (error 0x%02X, data 0x%lX)", sd2.errorCode(), (unsigned long) sd2.errorData());
}

// Reads SD2_TEST_SECTORS in transfers of n sectors. A link frame after each one switches SPI0
// back to the clock of the link. A failed read is counted, and the card is set up again.
static bool read_pass(Print &out, uint8_t *buf, uint8_t mhz, uint8_t n, uint32_t ref_crc, uint32_t &crc) {
  uint32_t us = 0, errors = 0;
  crc = KG_CRC32_INIT;
  for (uint32_t s = 0; s < SD2_TEST_SECTORS; s += n) {
    hang_step(SD2_HANG_STEP);
    uint32_t t = us_now();
    bool ok = read_once(s, buf, n);
    us += us_now() - t;
    if (!ok) {
      if (errors++ == 0) { out.printf(" x%u read of %lu failed", n, (unsigned long) s); print_error(out); }
      if (!sd2_begin_mhz(mhz)) { out.print(", init failed"); print_error(out); return false; }
      continue;
    }
    crc = kg_crc32_update(crc, buf, n * 512);
    spi_send(CMD_NOP, 0, 0);
  }
  crc = kg_crc32_final(crc);
  if (errors) { out.printf(" %lu errors", (unsigned long) errors); return false; }
  out.printf(" x%u %lu KB/s", n, (unsigned long) ((uint64_t) SD2_TEST_SECTORS * 512 * 1000000 / 1024 / us));
  if (ref_crc && crc != ref_crc) { out.printf(" crc %08lX MISMATCH", (unsigned long) crc); return false; }
  return true;
}

void sd2_test(Print &out) {
  static uint8_t buf[SD2_BLOCK * 512];
  if (!sd2_begin_mhz(test_mhz[0])) {
    out.print("+SD2:no card (or a boot core without SD2)"); print_error(out); out.print("\r\n");
    out.print("ERROR\r\n");
    return;
  }
  uint32_t sectors = sd2_sector_count();
  cid_t cid;
  bool cid_ok;
  {
    Sd2Bus g;
    cid_ok = sd2.readCID(&cid);
  }
  static const char *types[] = { "?", "SD1", "SD2", "SDHC" };
  out.printf("+SD2:%s, %lu MB", types[sd2.type() < 4 ? sd2.type() : 0], (unsigned long) (sectors / 2048));
  if (cid_ok) {
    out.printf(", mid %02X oid %.2s pnm %.5s psn %08lX", cid.mid, cid.oid, cid.pnm, (unsigned long) cid.psn());
  }
  out.print("\r\n");

  uint32_t ref_crc = 0;
  for (uint8_t mhz : test_mhz) {
    out.printf("+SD2:%u MHz", mhz);
    if (!sd2_begin_mhz(mhz)) { out.print(" init failed"); print_error(out); out.print("\r\n"); break; }
    read_once(0, buf, 1);
    out.printf(" (%lu kHz):", (unsigned long) (spi_get_baudrate(spi0) / 1000));
    uint32_t crc1, crc8;
    bool ok = read_pass(out, buf, mhz, 1, ref_crc, crc1);
    if (ok && !ref_crc) ref_crc = crc1;
    ok = read_pass(out, buf, mhz, SD2_BLOCK, ref_crc, crc8) && ok;
    if (ok && !ref_crc) ref_crc = crc8;
    out.print(ok ? " OK\r\n" : " FAIL\r\n");
  }
  out.printf("+SD2:crc of the first %u KB %08lX\r\n", SD2_TEST_SECTORS / 2, (unsigned long) ref_crc);
  out.print("OK\r\n");
}
