#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_file_loader_read_list(void);
void app_file_loader_menu(uint8_t vpos);
void app_file_loader_overlay(bool initSD);
void app_file_loader_save();
void app_file_loader_send_file(uint16_t file_id);
void app_file_loader_on_keyboard();
// a new core: its first list loads the last file
void app_file_loader_core_loaded(void);
// the core menu (Options) takes the file browser: remember the cursor, read the list again later
void app_file_loader_leave(void);

#ifdef __cplusplus
}
#endif
