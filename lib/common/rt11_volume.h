#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One block of the volume in each call, 0 = done
typedef struct {
  int (*read)(void *ctx, unsigned blk, void *buf);
  int (*write)(void *ctx, unsigned blk, const void *buf);
  void *ctx;
} rt11_io_t;

// The file to add: the next 512 bytes into buf (zeroed before), 0 = done
typedef int (*rt11_src_fn)(void *arg, uint8_t *buf);

// RT-11's date word: year 1972.., month 1..12, day 1..31, 0 if out of range
unsigned rt11sav_date(int year, int month, int day);

// Adds fname (a FAT name, cut to six RAD50 chars, a digit instead of the last one if the volume
// has that .SAV already) to the volume and appends "R name" to its STARTS.COM. prog (7 bytes) gets
// the RT-11 name used. 0, or -1 with a short reason in err.
int rt11_build(const rt11_io_t *io, const char *fname, unsigned size, unsigned date,
               rt11_src_fn src, void *arg, char *prog, const char **err);

#ifdef __cplusplus
}
#endif
