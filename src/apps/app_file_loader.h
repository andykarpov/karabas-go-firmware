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

#ifdef __cplusplus
}
#endif
