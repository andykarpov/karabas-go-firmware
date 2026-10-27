#pragma once

// Reads karabas.ini and the pad drivers (drivers/*.ini) from the whole file in memory, by the
// rules of the IniFile library that read them before: a line ends at \n, \r, \r\n or \n\r;
// lines starting with ';' or '#' are comments; [section] and key = value are matched ignoring
// case and the spaces around them; a value is the rest of the line after '=', trimmed (a ';'
// in it is part of the value); the first line with the key in the section wins.

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// IniFile::validate with a read buffer of buf_len bytes: every line but the last one fits in
// buf_len - 2 chars (karabas.ini: 80 bytes, 78 chars). A file that fails is not read at all.
bool kg_ini_valid(const char *text, size_t len, size_t buf_len);
// false if the section or the key is missing, or the value does not fit out_size
bool kg_ini_get(const char *text, size_t len, const char *section, const char *key,
                char *out, size_t out_size);
// true / yes / 1 and false / no / 0, any case; false for another value
bool kg_ini_get_bool(const char *text, size_t len, const char *section, const char *key, bool *val);

#ifdef __cplusplus
}
#endif
