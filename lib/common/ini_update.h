#pragma once

// Writes new values into an ini file (karabas.ini) line by line, keeping everything else:
// comments, blank lines, unknown keys and their order. Feed the lines of the old file without
// their line ends, then call kg_ini_update_end(); the lines of the new file come out of the
// callback. A key that is not in its section yet is added at the end of the section (before the
// blank lines that end it), a section that is missing is added at the end of the file. Sections
// and keys are matched ignoring case and the spaces around them, as IniFile reads them; every
// line of the section with the key gets the new value.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KG_INI_LINE_MAX 78 // chars of a line: kg_ini_valid() with the 80-byte buffer of load_setup()
#define KG_INI_NAME_MAX 15
#define KG_INI_VALUE_MAX 32

typedef struct {
  const char *section;
  const char *key;
  char value[KG_INI_VALUE_MAX + 1];
  bool done; // written out
} kg_ini_item_t;

typedef void (*kg_ini_out_t)(void *ctx, const char *line);

typedef struct {
  kg_ini_item_t *items;
  uint8_t count;
  char section[KG_INI_NAME_MAX + 1]; // the section of the last lines, "" before the first one
  uint8_t blanks;                    // blank lines held back
  bool any;                          // a line went out
  kg_ini_out_t out;
  void *ctx;
} kg_ini_update_t;

void kg_ini_update_begin(kg_ini_update_t *u, kg_ini_item_t *items, uint8_t count, kg_ini_out_t out, void *ctx);
void kg_ini_update_line(kg_ini_update_t *u, const char *line);
void kg_ini_update_end(kg_ini_update_t *u);

#ifdef __cplusplus
}
#endif
