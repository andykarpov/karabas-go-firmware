#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "fpga/core_list.h"
#include "ui/sorts.h"
#include "osd.h"
#include "apps/app_core.h"

core_list_item_t *cores = NULL; // on the heap, cores_len items
uint8_t cores_len = 0;
#define CORES_CHUNK 16
static uint8_t cores_cap = 0;

bool core_list_get_item(const char *filename, core_list_item_t *core) {
  core_file_t cf;
  if (!core_file_open(&file1, filename, &cf)) return false;
  snprintf(core->filename, sizeof(core->filename), "%s", filename);
  core_file_info(&file1, &cf, core);
  return true;
}

// the next free item of cores[], NULL at MAX_CORES or out of memory
static core_list_item_t *add_core(void) {
  if (cores_len >= MAX_CORES) {
    d_printf("Core list: more than %d cores, the rest is left out", MAX_CORES); d_println();
    return NULL;
  }
  if (cores_len >= cores_cap) {
    uint16_t c = cores_cap + CORES_CHUNK;
    if (c > MAX_CORES) c = MAX_CORES;
    core_list_item_t *p = (core_list_item_t *) realloc(cores, c * sizeof(core_list_item_t));
    if (p == NULL) {
      d_printf("Core list: out of memory at %d cores", cores_len); d_println();
      return NULL;
    }
    cores = p;
    cores_cap = (uint8_t) c;
  }
  return &cores[cores_len++];
}

void core_list_sort(void) {
  if (cores_len) cores_sort(cores, cores_len, hw_setup.core_sort);
}

void core_list_read(void) {
  free(cores);
  cores = NULL;
  cores_len = 0;
  cores_cap = 0;
  if (has_sd && sd_opendir(&root1, "/")) {
    FILINFO fi;
    while (f_readdir(&root1, &fi) == FR_OK && fi.fname[0]) {
      if (!(fi.fattrib & AM_DIR) && strlen(fi.fname) < 32 && core_file_is_core(fi.fname) &&
          sd_open(&file1, fi.fname, FA_READ)) {
        core_list_item_t item;
        bool ok = core_list_get_item(fi.fname, &item); // a .kg of other boards is left out
        sd_close(&file1);
        if (!ok) continue;
        core_list_item_t *p = add_core();
        if (!p) break;
        *p = item;
      }
    }
    // give the unused end of the last chunk back
    if (cores_len > 0 && cores_len < cores_cap) {
      core_list_item_t *p = (core_list_item_t *) realloc(cores, cores_len * sizeof(core_list_item_t));
      if (p) { cores = p; cores_cap = cores_len; }
    }
  }
  core_list_sort();
}

// the screen a core starts on
static void set_core_screen(void) {
  switch (core.type) {
    case CORE_TYPE_BOOT: osd_state = state_core_browser; break;
    case CORE_TYPE_OSD: osd_state = state_main; break;
    case CORE_TYPE_FILELOADER: osd_state = state_file_loader; break;
    default: osd_state = state_main;
  }
}

void core_list_start_file(const char *filename) {
  bool from_boot = core.type == CORE_TYPE_BOOT;
  app_core_leave_browser();
  if (from_boot) do_configure(filename);
  else do_configure_clean(filename);
  set_core_screen();
}

// back to the boot core from a running core without an MCU reboot (back_to_menu() of input.c): the
// USB host and its devices stay as they are. The boot core clears the SRAM itself when it starts.
// Also from the boot core itself (AT+KARABAS=reset, load:boot).
void core_list_return(void) {
  if (!has_sd || !sd_exists(core_file_boot())) {
    flush_and_reboot(); // no boot core to load: the reboot says so
    return;
  }
  d_println("Back to the boot core");
  app_core_leave_browser();
  do_configure(core_file_boot());
  ui_osd_reset_timers();
  osd_state = state_core_browser;
  core_list_read();
  osd_handle(true);
}
