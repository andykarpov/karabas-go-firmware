#pragma once

#include <stddef.h>

// File name helpers for the OSD file lists. The lists use the long names: exFAT has no 8.3
// names, so FAT and exFAT cards work the same way.

// true if the last 4 chars of name (".trd") are in exts (".trd,.scl"), ignoring case.
// An empty exts matches every name.
bool kg_name_has_ext(const char *name, const char *exts);

// name shortened to fit width chars: "Prince of Persia.trd" -> "Princ~.trd" for width 10.
// out must hold width + 1 bytes.
void kg_short_name(const char *name, char *out, size_t width);

// The file name saved for an F slot that is a physical floppy drive (Karabas Go): ":phys0" /
// ":phys1" (':' is not in FAT names). kg_phys_drive gives the drive (0 / 1) of such a name, -1
// for any other name; kg_phys_name writes the name of a drive (out: at least 7 bytes).
int kg_phys_drive(const char *name);
void kg_phys_name(int drive, char *out);
