#include "main.h"
#include "uknc_sav.h"
#include "file_name.h"
#include "rt11_volume.h"
#include "ui/ui_draw.h"
#include "apps/app_core.h"
#include "diag/hang_diag.h"

#include <stdlib.h>

#define MEDIA_DIR "/mc0511-media"
#define BASE_FILE "RT11BASE.DSK"
#define BASE_FILE2 "BASERT11.DSK" // the name tang-uknc ships it under (soft/)
#define DISK_FILE "RT11SAV.DSK"
#define OLD_FILE "RT11OLD.DSK"
#define BASE_PATH MEDIA_DIR "/" BASE_FILE
#define BASE_PATH2 MEDIA_DIR "/" BASE_FILE2
#define DISK_PATH MEDIA_DIR "/" DISK_FILE
#define OLD_PATH MEDIA_DIR "/" OLD_FILE
#define TMP_PATH MEDIA_DIR "/RT11NEW.TMP"

#define SAV_SLOT 5
#define FDD0_SLOT 0
#define FLOPPY_SLOTS 4
#define DISK_SIZE 819200u
#define COPY_SIZE 4096u
#define POPUP_STEP (64 * 1024u) // an OSD redraw costs

// three FILs and the copy buffer: only while a disk is made, the core is the one user
typedef struct {
  FIL base, sav, out;
  uint8_t buf[COPY_SIZE];
} work_t;

bool uknc_sav_slot(uint8_t slot) { return slot == SAV_SLOT && strcmp(core.id, "uknc") == 0; }

// the block i/o and the SAV source feed the hang watchdog (debug=1) themselves: a slow card writes
// a long program for seconds
static int block_read(void *ctx, unsigned blk, void *buf) {
  UINT n;
  hang_step(1);
  return f_lseek((FIL *) ctx, (FSIZE_t) blk * 512) != FR_OK || f_read((FIL *) ctx, buf, 512, &n) != FR_OK || n != 512;
}

static int block_write(void *ctx, unsigned blk, const void *buf) {
  UINT n;
  hang_step(2);
  return f_lseek((FIL *) ctx, (FSIZE_t) blk * 512) != FR_OK || f_write((FIL *) ctx, buf, 512, &n) != FR_OK || n != 512;
}

static int source_read(void *arg, uint8_t *buf) {
  UINT n;
  hang_step(3);
  return f_read((FIL *) arg, buf, 512, &n) != FR_OK;
}

static int item_of_name(const char *name) {
  for (uint8_t i = 0; i < core.osd_len; i++) {
    if (strcmp(core.osd[i].name, name) == 0) return i;
  }
  return -1;
}

static int item_of_slot(uint8_t slot) {
  for (uint8_t i = 0; i < core.osd_len; i++) {
    if (core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER && core.osd[i].slot_id == slot) return i;
  }
  return -1;
}

// the new disk in TMP_PATH; the template and what is mounted stay as they are
static const char *make_disk(work_t *w, const char *sav_path, const char *sav_name) {
  if (!sd_open(&w->base, BASE_PATH, FA_READ) && !sd_open(&w->base, BASE_PATH2, FA_READ)) return "No " BASE_FILE;
  const char *error = NULL;
  if (f_size(&w->base) != DISK_SIZE) error = "Bad " BASE_FILE;
  else if (!sd_open(&w->sav, sav_path, FA_READ)) error = "Cannot open SAV";
  else {
    uint32_t size = f_size(&w->sav);
    if (size == 0 || size > DISK_SIZE) error = "Bad SAV size";
    else if (!sd_open(&w->out, TMP_PATH, FA_READ | FA_WRITE | FA_CREATE_ALWAYS)) error = "Cannot create disk";
    else {
      for (uint32_t pos = 0; pos < DISK_SIZE && !error; pos += COPY_SIZE) {
        if (sd_read(&w->base, w->buf, COPY_SIZE) != COPY_SIZE || sd_write(&w->out, w->buf, COPY_SIZE) != COPY_SIZE) {
          error = "Disk copy failed";
        }
        hang_step(0); // a slow card takes seconds
        if (pos % POPUP_STEP == 0) {
          osd_progress_popup("Run SAV", pos, DISK_SIZE);
          osd_update();
        }
      }
      if (!error) {
        rt11_io_t io = { block_read, block_write, &w->out };
        kg_datetime_t dt;
        unsigned date = rtc_now(&dt) ? rt11sav_date(dt.year, dt.month, dt.day) : 0;
        char prog[8];
        const char *reason = "";
        if (rt11_build(&io, sav_name, size, date, source_read, &w->sav, prog, &reason) != 0) {
          error = reason;
        } else {
          d_printf("Run SAV: %s as %s.SAV", sav_name, prog); d_println();
        }
      }
      sd_close(&w->out); // f_close syncs
    }
    sd_close(&w->sav);
  }
  sd_close(&w->base);
  return error;
}

// TMP_PATH becomes the disk; no slot holds the file before the rename
static const char *install_disk(void) {
  for (uint8_t i = 0; i < FLOPPY_SLOTS; i++) {
    core_file_slot_t *fs = &file_slots[i];
    if (fs->is_mounted && strcmp(fs->dir, MEDIA_DIR) == 0 && strcmp(fs->filename, DISK_FILE) == 0) {
      fs->is_mounted = false;
      if (i != FDD0_SLOT) fs->filename[0] = '\0';
      img_mount(i);
    }
  }
  bool has_old = sd_exists(DISK_PATH);
  if (has_old) {
    if (sd_exists(OLD_PATH)) f_unlink(OLD_PATH);
    if (f_rename(DISK_PATH, OLD_PATH) != FR_OK) return "Cannot keep old disk"; // FAT: no rename over a file
  }
  if (f_rename(TMP_PATH, DISK_PATH) != FR_OK) {
    if (has_old) f_rename(OLD_PATH, DISK_PATH);
    return "Cannot install disk";
  }
  return NULL;
}

bool uknc_sav_run(uint8_t slot) {
  const char *error = NULL;
  // zeroed: sd_open() closes a FIL that looks open first, and a FIL of malloc() garbage looked
  // open (f_close on it: a fault, the reboot after a SAV was chosen)
  work_t *w = calloc(1, sizeof(work_t));
  if (!w) {
    error = "No memory";
  } else {
    char path[KG_PATH_MAX];
    kg_path_join(path, sizeof(path), file_slots[slot].dir, file_slots[slot].filename);
    kg_path_collapse(path);
    osd_progress_popup("Run SAV", 0, DISK_SIZE);
    osd_update();
    error = make_disk(w, path, file_slots[slot].filename);
    free(w);
    if (!error) error = install_disk();
    if (error) f_unlink(TMP_PATH);
    osd_hide_popup();
  }
  if (error) {
    d_printf("Run SAV: %s", error); d_println();
    ui_box("Run SAV", OSD_COLOR_RED_I, error, OSD_COLOR_RED_I);
    osd_update();
    ms_delay(2000);
    return false;
  }

  core_file_slot_t *fdd0 = &file_slots[FDD0_SLOT];
  snprintf(fdd0->dir, sizeof(fdd0->dir), "%s", MEDIA_DIR);
  snprintf(fdd0->filename, sizeof(fdd0->filename), "%s", DISK_FILE);
  fdd0->is_mounted = true;
  int item = item_of_name("FDD controller");
  if (item >= 0 && core.osd[item].val != 1) {
    core.osd[item].val = 1;
    core_send(item);
    app_core_save(item);
  }
  img_mount(FDD0_SLOT);
  item = item_of_slot(FDD0_SLOT);
  if (item >= 0) app_core_save(item);
  item = item_of_name("Reset");
  if (item >= 0) core_trigger(item);
  return true;
}
