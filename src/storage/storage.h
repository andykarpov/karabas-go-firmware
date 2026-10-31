#pragma once

#include "ff.h"
#include "sd_card.h"

#ifdef __cplusplus
extern "C" {
#endif

// The SD card of the MCU (SD1) through FatFs (third_party/fatfs) on sd_card over the PIO SPI. Only
// APP uses the file system; in MSC mode the PC gets the card (sd1_card()) past FatFs.
// Paths are full ("/dir/name", or "name" in the root): FatFs has no current dir here.

extern FIL file1, file2; // file1: the core file (file.cpp), file2: an external ROM
extern DIR root1;        // the dir of the file list on the screen
extern bool has_sd;

void storage_begin();
sd_card_t *sd1_card();
// before a reboot of the MCU alone: end the multi-sector transfer that sd_card keeps open on the
// dedicated bus of SD1. APP only; nothing in MSC mode (the card is the PC's, the eject ends it).
void sd1_sync_before_reboot();
bool sd_is_exfat();
bool sd_exists(const char *path);
bool sd_is_dir(const char *path);
static inline bool sd_is_open(const FIL *f) { return f->obj.fs != NULL; }
static inline bool sd_dir_is_open(const DIR *d) { return d->obj.fs != NULL; }
// close if open
void sd_close(FIL *f);
// f_open after closing f (FA_READ, FA_READ | FA_WRITE, FA_WRITE | FA_CREATE_ALWAYS, ...)
bool sd_open(FIL *f, const char *path, BYTE mode);
bool sd_opendir(DIR *d, const char *path);
void sd_closedir(DIR *d);
// f_read / f_write of len bytes; the count done, 0 on an error
UINT sd_read(FIL *f, void *buf, UINT len);
UINT sd_write(FIL *f, const void *buf, UINT len);
// One FIL for the short file jobs of APP, which open, use and close it in one function (a FIL
// has its own 512-byte buffer, so one is shared instead of a static one per job). NULL if the
// file can't be opened, or the FIL is taken (a nested use: logged). sd_tmp_close() is f_close().
FIL *sd_tmp_open(const char *path, BYTE mode);
bool sd_tmp_close(void);
// the whole file in a malloc()ed buffer with a null after it (free() it); NULL if it can't
// be read or is longer than max
char *sd_read_all(const char *path, size_t max, size_t *len);
// the dirs of a path, each made if missing
bool sd_mkdirs(const char *path);

// File ids of the OSD file lists: the dir entry index of a file in its dir (SdFat's dirIndex:
// the short name entry on FAT, the file entry on exFAT; FILINFO.dir_ofs / 32). 0 means no file
// (the "." / ".." / eject entries, the saved file of the file loader). The file loader saves
// the id of the last file into the core file (16 bits). false: the dir is too big for 16 bit ids.
bool sd_file_id(const FILINFO *fi, uint16_t *id);
// the item of an id in the open dir
bool sd_find_id(DIR *dir, uint16_t id, FILINFO *fi);

#ifdef __cplusplus
}
#endif
