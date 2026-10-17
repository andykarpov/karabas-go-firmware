#pragma once

#include "SdFat.h"

extern SdFs sd1;
extern FsFile file1, file2;
extern FsFile root1;
extern bool has_sd;

void storage_begin();

// File ids of the OSD file lists: the dir entry index of a file in its dir. 0 means no file
// (the "." / ".." / eject entries, the saved file of the file loader). The file loader saves
// the id of the last file into the core file (16 bits). false: the dir is too big for 16 bit ids.
bool sd_file_id(FsFile &f, uint16_t &id);
bool sd_open_id(FsFile &f, FsFile *dir, uint16_t id, oflag_t oflag = O_RDONLY);
