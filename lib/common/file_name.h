#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// File name helpers for the OSD file lists. The lists use the long names: exFAT has no 8.3
// names, so FAT and exFAT cards work the same way.

// true if name ends with one of the extensions of exts (".trd,.scl", ".img .vhd"; separated
// by ',', ';' or white space, the dot optional, any length), ignoring case. An empty exts
// matches every name.
bool kg_name_has_ext(const char *name, const char *exts);

// the extensions of exts as shown in the OSD: ".trd .scl", cut with ".." to fit width chars.
// out must hold width + 1 bytes; empty for an empty exts.
void kg_exts_text(const char *exts, char *out, size_t width);

// name shortened to fit width chars: "Prince of Persia.trd" -> "Princ~.trd" for width 10.
// out must hold width + 1 bytes.
void kg_short_name(const char *name, char *out, size_t width);

// The file name saved for an F slot that is a physical floppy drive (Karabas Go): ":phys0" /
// ":phys1" (':' is not in FAT names). kg_phys_drive gives the drive (0 / 1) of such a name, -1
// for any other name; kg_phys_name writes the name of a drive (out: at least 7 bytes).
int kg_phys_drive(const char *name);
void kg_phys_name(int drive, char *out);

// String and path helpers in place of Arduino's String.

// a path on the SD card: a dir (256) + '/' + a name (256)
#define KG_PATH_MAX 514

// strips the white space at both ends, in place
void kg_trim(char *s);
// ASCII lower case, in place
void kg_str_lower(char *s);
// out = dir + "/" + name; false if it did not fit (out is cut then)
bool kg_path_join(char *out, size_t size, const char *dir, const char *name);
// every run of '/' becomes one: "//games//x" -> "/games/x", in place
void kg_path_collapse(char *path);
// a dir from the root, in place: "" -> "/", "games" -> "/games" (cut to size if it does not fit)
void kg_path_root(char *dir, size_t size);
// cuts the path at its last '/': "/games/zx" -> "/games", "/games" -> ""; no '/': unchanged
void kg_path_parent(char *path);

#ifdef __cplusplus
}
#endif
