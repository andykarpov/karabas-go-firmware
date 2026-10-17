#include "main.h"

// SD card on a dedicated PIO SPI

static PioSPI spiSD(PIN_SD_SPI_TX, PIN_SD_SPI_RX, PIN_SD_SPI_SCK, SD_CS_PIN, SPI_MODE0, SD_SCK_MHZ(16)); // dedicated SD1 SPI
#define SD_CONFIG  SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(16), &spiSD) // SD1 SPI Settings

SdFs sd1;
FsFile file1, file2;
FsFile root1;
bool has_sd = false;

void storage_begin() {
  // SD
  d_print("Mounting SD card... ");
  if (!sd1.begin(SD_CONFIG)) {
    has_sd = false;
    d_println("Failed");
    sd1.initErrorPrint(&Serial);
  } else {
    has_sd = true;
    d_println(sd1.fatType() == FAT_TYPE_EXFAT ? "Done (exFAT)" : "Done");
  }
}

// FAT: the index itself, as saved by older firmware (0 is the "." entry of a subdir, in the
// root the volume label if the card has one). exFAT has no "." entries, so the first file of
// a subdir has index 0: the id is the index + 1 there.
static uint32_t id_offset() {
  return sd1.fatType() == FAT_TYPE_EXFAT ? 1 : 0;
}

bool sd_file_id(FsFile &f, uint16_t &id) {
  uint32_t i = f.dirIndex() + id_offset();
  id = i;
  return i <= 0xFFFF;
}

bool sd_open_id(FsFile &f, FsFile *dir, uint16_t id, oflag_t oflag) {
  if (id < id_offset()) return false;
  return f.open(dir, id - id_offset(), oflag);
}
