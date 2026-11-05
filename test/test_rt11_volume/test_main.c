#include <unity.h>
#include <string.h>
#include "rt11_volume.h"

// A small volume: home block 1 (directory at block 6), one segment, entries
// STARTS.COM (2 blocks at 20), DIR.SAV (3 blocks), an empty area, end of segment.
#define BLOCKS 200
#define DIR_BLOCK 6
#define DATA_BLOCK 20
#define ENTRY 14
#define E_MPTY 0x0200
#define E_PERM 0x0400
#define E_EOS 0x0800

static uint8_t disk[BLOCKS * 512];
static int fail_write_at = -1;

static int blk_read(void *ctx, unsigned blk, void *buf) {
  (void) ctx;
  if (blk >= BLOCKS) return 1;
  memcpy(buf, disk + blk * 512, 512);
  return 0;
}

static int blk_write(void *ctx, unsigned blk, const void *buf) {
  (void) ctx;
  if (blk >= BLOCKS || (int) blk == fail_write_at) return 1;
  memcpy(disk + blk * 512, buf, 512);
  return 0;
}

static const rt11_io_t io = { blk_read, blk_write, NULL };

static void put16(unsigned off, unsigned v) {
  disk[off] = v & 255;
  disk[off + 1] = v >> 8;
}

static unsigned get16(unsigned off) { return disk[off] | disk[off + 1] << 8; }

static unsigned r50(const char *s) {
  static const char set[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.?0123456789";
  unsigned v = 0;
  for (int i = 0; i < 3; i++) v = v * 40 + (unsigned) (strchr(set, s[i]) - set);
  return v;
}

static unsigned add_entry(unsigned off, unsigned status, const char *n1, const char *n2, const char *ext, unsigned len) {
  put16(off, status);
  put16(off + 2, r50(n1));
  put16(off + 4, r50(n2));
  put16(off + 6, r50(ext));
  put16(off + 8, len);
  return off + ENTRY;
}

void setUp() {
  fail_write_at = -1;
  memset(disk, 0, sizeof(disk));
  put16(512 + 0724, DIR_BLOCK);
  unsigned seg = DIR_BLOCK * 512;
  put16(seg, 1);              // segments
  put16(seg + 4, 1);          // highest
  put16(seg + 8, DATA_BLOCK); // first data block
  unsigned off = seg + 10;
  off = add_entry(off, E_PERM, "STA", "RTS", "COM", 2);
  off = add_entry(off, E_PERM, "DIR", "   ", "SAV", 3);
  off = add_entry(off, E_MPTY, "   ", "   ", "   ", BLOCKS - DATA_BLOCK - 5);
  put16(off, E_EOS);
  memcpy(disk + DATA_BLOCK * 512, "VERSION\r\n", 9);
}

void tearDown() {}

static const uint8_t *src_data;
static unsigned src_left;

static int src(void *arg, uint8_t *buf) {
  (void) arg;
  unsigned n = src_left < 512 ? src_left : 512;
  memcpy(buf, src_data, n);
  src_data += n;
  src_left -= n;
  return 0;
}

static int build(const char *name, const void *data, unsigned size, char *prog, const char **err) {
  src_data = data;
  src_left = size;
  *err = "";
  return rt11_build(&io, name, size, 0, src, NULL, prog, err);
}

// the block of the file on the volume, or 0
static unsigned find(const char *n1, const char *n2, const char *ext, unsigned *len) {
  unsigned seg = DIR_BLOCK * 512, blk = get16(seg + 8);
  for (unsigned off = seg + 10; !(get16(off) & E_EOS); off += ENTRY) {
    if ((get16(off) & E_PERM) && get16(off + 2) == r50(n1) && get16(off + 4) == r50(n2) && get16(off + 6) == r50(ext)) {
      *len = get16(off + 8);
      return blk;
    }
    blk += get16(off + 8);
  }
  return 0;
}

static void test_date_word() {
  TEST_ASSERT_EQUAL_UINT(0, rt11sav_date(1971, 1, 1));
  TEST_ASSERT_EQUAL_UINT(0, rt11sav_date(2026, 13, 1));
  TEST_ASSERT_EQUAL_UINT(1 << 14 | 11 << 10 | 5 << 5 | 22, rt11sav_date(2026, 11, 5));
}

static void test_file_added_and_started() {
  static uint8_t sav[1000];
  memset(sav, 0xA5, sizeof(sav));
  char prog[8];
  const char *err;
  TEST_ASSERT_EQUAL_INT(0, build("kkvtst.sav", sav, sizeof(sav), prog, &err));
  TEST_ASSERT_EQUAL_STRING("KKVTST", prog);

  unsigned len = 0, blk = find("KKV", "TST", "SAV", &len);
  TEST_ASSERT_NOT_EQUAL_UINT(0, blk);
  TEST_ASSERT_EQUAL_UINT(2, len);
  TEST_ASSERT_EQUAL_MEMORY(sav, disk + blk * 512, sizeof(sav));
  TEST_ASSERT_EQUAL_UINT8(0, disk[blk * 512 + sizeof(sav)]); // zero padded

  blk = find("STA", "RTS", "COM", &len);
  TEST_ASSERT_NOT_EQUAL_UINT(0, blk);
  TEST_ASSERT_EQUAL_STRING_LEN("VERSION\r\nR KKVTST\r\n\0", disk + blk * 512, 20);
}

static void test_system_file_is_not_replaced() {
  uint8_t sav[512] = { 1 };
  char prog[8];
  const char *err;
  TEST_ASSERT_EQUAL_INT(0, build("dir.sav", sav, sizeof(sav), prog, &err));
  TEST_ASSERT_EQUAL_STRING("DIR1", prog);
  unsigned len = 0;
  TEST_ASSERT_NOT_EQUAL_UINT(0, find("DIR", "   ", "SAV", &len));
  TEST_ASSERT_EQUAL_UINT(3, len);
}

static void test_long_and_odd_names() {
  uint8_t sav[512] = { 1 };
  char prog[8];
  const char *err;
  TEST_ASSERT_EQUAL_INT(0, build("a-long_name.v2.sav", sav, sizeof(sav), prog, &err));
  TEST_ASSERT_EQUAL_STRING("ALONGN", prog);
  setUp();
  TEST_ASSERT_EQUAL_INT(0, build("---.sav", sav, sizeof(sav), prog, &err));
  TEST_ASSERT_EQUAL_STRING("PROG", prog);
}

static void test_disk_full() {
  static uint8_t sav[BLOCKS * 512];
  char prog[8];
  const char *err;
  TEST_ASSERT_EQUAL_INT(-1, build("big.sav", sav, sizeof(sav), prog, &err));
  TEST_ASSERT_EQUAL_STRING("disk full", err);
}

static void test_write_error() {
  uint8_t sav[512] = { 1 };
  char prog[8];
  const char *err;
  fail_write_at = DIR_BLOCK;
  TEST_ASSERT_EQUAL_INT(-1, build("x.sav", sav, sizeof(sav), prog, &err));
  TEST_ASSERT_EQUAL_STRING("dir write", err);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_date_word);
  RUN_TEST(test_file_added_and_started);
  RUN_TEST(test_system_file_is_not_replaced);
  RUN_TEST(test_long_and_odd_names);
  RUN_TEST(test_disk_full);
  RUN_TEST(test_write_error);
  return UNITY_END();
}
