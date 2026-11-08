#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t curr_osd_item;

void app_core_overlay(bool force);
void app_core_menu(uint8_t vpos);
void app_core_save(uint8_t pos);
void app_core_on_keyboard();
int app_core_init_filebrowser();
void app_core_filebrowser(uint8_t vpos);
void app_core_on_select_file();
// AT+KARABAS (the file browser of the OSD, if open, is closed first):
// an image for the file item pos, as chosen in the browser; false: no such file, a wrong extension
bool app_core_mount_path(uint8_t pos, const char *path);
void app_core_eject_item(uint8_t pos);
bool app_core_in_browser(void);
void app_core_leave_browser(void);

#ifdef __cplusplus
}
#endif
