#pragma once

#include "SdFat.h"

extern SdFat32 sd1;
extern File32 file1, file2;
extern File32 root1;
extern bool has_sd;

void storage_begin();
