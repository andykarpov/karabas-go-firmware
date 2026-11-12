#pragma once

// The file browser of the text OSD: a sorted list of the files of a dir on the SD card (by their
// ids, sd_file_id), drawn a page at a time with the names cached, moved by the navigation keys.
// The file loader and the F / FL items of the core menu fill it with what they show.
// Entries without a file (id 0) are the ones of the core menu: the eject entry, the physical
// drives of the Karabas Go, "." (the root) and ".." (the parent); they sort before the files.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// filling: the list empty, the selection on the first entry
void fb_clear(void);
// a file / a subdir of the dir by its name and id; false if the list is full
bool fb_add_file(const char *name, uint16_t id);
bool fb_add_dir(const char *name, uint16_t id);
bool fb_add_eject(void);
bool fb_add_phys(uint8_t drive); // 0 / 1: shown by its name (kg_drive_label, core.drives)
bool fb_add_root(void);          // "."
bool fb_add_parent(void);        // ".."
// sorts the list (the entries without a file first, then the dirs) and drops the cached names
void fb_sort(void);

uint16_t fb_len(void);
uint16_t fb_sel(void);
void fb_select(uint16_t i);
uint16_t fb_id(uint16_t i);      // 0 for the entries without a file
bool fb_is_eject(uint16_t i);
int fb_phys(uint16_t i);         // the drive of a physical drive entry, else -1
bool fb_is_root(uint16_t i);
bool fb_is_parent(uint16_t i);
bool fb_is_dir(uint16_t i);      // a subdir (not the root or the parent)

// moves the selection by a navigation key (ui_list_key); true if the key was a move
bool fb_key(uint8_t key);
// draws the page of the selection from row top: the names (read from dir when the page changed),
// a line, then "Dir <dir>" when show_dir (its end, if it is long), and the page number (from
// column 8); returns its row
uint8_t fb_draw(uint8_t top, const char *dir, bool show_dir);
// the bitmap OSD (link v2): the page of the selection on the rows of ui2, exts_hint (the keys,
// the extensions) on the first hint line, the page and the dir on the second; sends the screen
void fb_draw_v2(const char *dir, const char *exts_hint);

#ifdef __cplusplus
}
#endif
