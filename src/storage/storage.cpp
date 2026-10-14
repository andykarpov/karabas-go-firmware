#include "main.h"

// SD card on a dedicated PIO SPI

static PioSPI spiSD(PIN_SD_SPI_TX, PIN_SD_SPI_RX, PIN_SD_SPI_SCK, SD_CS_PIN, SPI_MODE0, SD_SCK_MHZ(16)); // dedicated SD1 SPI
#define SD_CONFIG  SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(16), &spiSD) // SD1 SPI Settings

SdFat32 sd1;
File32 file1, file2;
File32 root1;
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
    d_println("Done");
  }
}
