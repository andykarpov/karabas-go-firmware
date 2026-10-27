#include "main.h"
#include "sd_pio.h"
#include "diskio.h"
#include "file_name.h"

// SD card on a dedicated PIO SPI

#define SD1_HZ 16000000

static sd_spi_bus_t sd1_bus;
static sd_card_t sd1;
static FATFS sd1_fs;
FIL file1, file2;
DIR root1;
bool has_sd = false;

sd_card_t *sd1_card() {
  return &sd1;
}

// FatFs disk I/O (ff.h): drive 0 is SD1
extern "C" DSTATUS disk_status(BYTE pdrv) {
  return pdrv == 0 && sd1.type ? 0 : STA_NOINIT;
}

extern "C" DSTATUS disk_initialize(BYTE pdrv) {
  return disk_status(pdrv);
}

extern "C" DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
  if (pdrv != 0 || !sd1.type) return RES_NOTRDY;
  return sd_card_read(&sd1, sector, buff, count) ? RES_OK : RES_ERROR;
}

extern "C" DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
  if (pdrv != 0 || !sd1.type) return RES_NOTRDY;
  return sd_card_write(&sd1, sector, buff, count) ? RES_OK : RES_ERROR;
}

extern "C" DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
  if (pdrv != 0 || !sd1.type) return RES_NOTRDY;
  switch (cmd) {
    case CTRL_SYNC: return sd_card_sync(&sd1) ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT: {
      uint32_t n = sd_card_sectors(&sd1);
      *(LBA_t *) buff = n;
      return n ? RES_OK : RES_ERROR;
    }
    case GET_SECTOR_SIZE: *(WORD *) buff = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *) buff = 1; return RES_OK;
    default: return RES_PARERR;
  }
}

// file dates from the RTC (the time read last by rtc_handle(), no I2C here)
extern "C" DWORD get_fattime(void) {
  kg_datetime_t dt;
  if (!rtc_has_clock() || !rtc_now(&dt)) {
    return ((DWORD) (FF_NORTC_YEAR - 1980) << 25) | ((DWORD) FF_NORTC_MON << 21) | ((DWORD) FF_NORTC_MDAY << 16);
  }
  return ((DWORD) (dt.year - 1980) << 25) | ((DWORD) dt.month << 21) | ((DWORD) dt.day << 16) |
         ((DWORD) dt.hour << 11) | ((DWORD) dt.minute << 5) | ((DWORD) dt.second >> 1);
}

void storage_begin() {
  d_print("Mounting SD card... ");
  if (!sd_pio_init(&sd1_bus, PIN_SD_SPI_SCK, PIN_SD_SPI_TX, PIN_SD_SPI_RX, SD_CS_PIN)) {
    has_sd = false;
    d_println("Failed (no PIO state machine for the SD card)");
    return;
  }
  if (!sd_card_begin(&sd1, &sd1_bus, SD1_HZ)) {
    has_sd = false;
    d_printf("Failed (card error %u, data %02X)", sd_card_error(&sd1), sd_card_error_data(&sd1)); d_println();
    return;
  }
  FRESULT res = f_mount(&sd1_fs, "", 1);
  if (res != FR_OK) {
    has_sd = false;
    d_printf("Failed (mount error %d)", (int) res); d_println();
    return;
  }
  has_sd = true;
  d_println(sd_is_exfat() ? "Done (exFAT)" : "Done");
}

bool sd_is_exfat() {
  return sd1_fs.fs_type == FS_EXFAT;
}

bool sd_exists(const char *path) {
  FILINFO fi;
  return has_sd && f_stat(path, &fi) == FR_OK;
}

bool sd_is_dir(const char *path) {
  FILINFO fi;
  // the root has no entry of its own
  if (!path[0] || !strcmp(path, "/")) return has_sd;
  return has_sd && f_stat(path, &fi) == FR_OK && (fi.fattrib & AM_DIR);
}

void sd_close(FIL *f) {
  if (sd_is_open(f)) f_close(f);
}

bool sd_open(FIL *f, const char *path, BYTE mode) {
  sd_close(f);
  return has_sd && f_open(f, path, mode) == FR_OK;
}

bool sd_opendir(DIR *d, const char *path) {
  sd_closedir(d);
  return has_sd && f_opendir(d, path) == FR_OK;
}

void sd_closedir(DIR *d) {
  if (sd_dir_is_open(d)) f_closedir(d);
}

UINT sd_read(FIL *f, void *buf, UINT len) {
  UINT n = 0;
  return f_read(f, buf, len, &n) == FR_OK ? n : 0;
}

UINT sd_write(FIL *f, const void *buf, UINT len) {
  UINT n = 0;
  return f_write(f, buf, len, &n) == FR_OK ? n : 0;
}

char *sd_read_all(const char *path, size_t max, size_t *len) {
  static FIL f; // not on the stack: a FIL has its own sector buffer
  if (!sd_open(&f, path, FA_READ)) return nullptr;
  FSIZE_t size = f_size(&f);
  char *buf = size <= max ? (char *) malloc(size + 1) : nullptr;
  if (buf && sd_read(&f, buf, size) != size) {
    free(buf);
    buf = nullptr;
  }
  sd_close(&f);
  if (!buf) return nullptr;
  buf[size] = '\0';
  *len = size;
  return buf;
}

bool sd_mkdirs(const char *path) {
  char p[KG_PATH_MAX];
  snprintf(p, sizeof(p), "%s", path);
  for (char *s = p + 1; ; s++) {
    if (*s == '/' || !*s) {
      char c = *s;
      *s = '\0';
      if (!sd_is_dir(p) && f_mkdir(p) != FR_OK) return false;
      if (!c) return true;
      *s = c;
    }
  }
}

// FAT: the index itself, as saved by older firmware (0 is the "." entry of a subdir, in the
// root the volume label if the card has one). exFAT has no "." entries, so the first file of
// a subdir has index 0: the id is the index + 1 there.
static uint32_t id_offset() {
  return sd_is_exfat() ? 1 : 0;
}

bool sd_file_id(const FILINFO *fi, uint16_t *id) {
  uint32_t i = fi->dir_ofs / 32 + id_offset();
  *id = i;
  return i <= 0xFFFF;
}

bool sd_find_id(DIR *dir, uint16_t id, FILINFO *fi) {
  if (id < id_offset()) return false;
  return f_readdir_at(dir, (DWORD) (id - id_offset()) * 32, fi) == FR_OK;
}
