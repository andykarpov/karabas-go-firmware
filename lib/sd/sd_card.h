#pragma once
// An SD card in SPI mode, the protocol of SdFat's SdSpiCard: CMD0 / CMD8 / ACMD41 / CMD58 at
// the start, single (CMD17 / CMD24) and multiple (CMD18 / CMD25) sector transfers, CSD / CID.
// The bus is a set of callbacks (sd_spi_bus_t): the PIO SPI of SD1 (sd_pio) or SPI0 under
// the bus lock (SD2, src/storage/sd2). On a dedicated bus a multiple sector transfer stays
// open between calls and the next call that goes on from its sector continues it (the USB
// MSC speed); sd_card_sync() ends it. Pure C.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void (*acquire)(void *ctx);          // the bus for a transfer (a shared bus: its lock, the clock)
  void (*release)(void *ctx);
  void (*cs)(void *ctx, bool selected);
  void (*clock)(void *ctx, uint32_t hz);
  void (*send)(void *ctx, const uint8_t *buf, size_t n);
  void (*recv)(void *ctx, uint8_t *buf, size_t n); // sends 0xFF
  void *ctx;
  bool dedicated;                      // nothing else on the bus: transfers may stay open
} sd_spi_bus_t;

typedef enum {
  SD_CARD_NONE = 0,
  SD_CARD_SD1,   // SDSC v1
  SD_CARD_SD2,   // SDSC v2
  SD_CARD_SDHC,  // SDHC / SDXC: block addresses
} sd_card_type_t;

// errors (sd_card_error): the step that failed
typedef enum {
  SD_ERR_NONE = 0,
  SD_ERR_CMD0, SD_ERR_CMD8, SD_ERR_ACMD41, SD_ERR_CMD58, SD_ERR_CMD9, SD_ERR_CMD10,
  SD_ERR_CMD12, SD_ERR_CMD13, SD_ERR_CMD17, SD_ERR_CMD18, SD_ERR_CMD24, SD_ERR_CMD25,
  SD_ERR_READ_TOKEN, SD_ERR_READ_TIMEOUT, SD_ERR_WRITE_DATA, SD_ERR_WRITE_TIMEOUT,
  SD_ERR_WRITE_STOP, SD_ERR_CSD,
} sd_err_t;

typedef struct {
  const sd_spi_bus_t *bus;
  sd_card_type_t type;
  uint8_t state;        // an open multiple sector transfer (dedicated bus)
  uint32_t cur_sector;  // the next sector of that transfer
  uint8_t err;          // sd_err_t of the last failure
  uint8_t err_data;     // the R1 or token of it
  uint32_t hz;
} sd_card_t;

// the card started at 400 kHz, then run at hz
bool sd_card_begin(sd_card_t *card, const sd_spi_bus_t *bus, uint32_t hz);
bool sd_card_read(sd_card_t *card, uint32_t sector, uint8_t *dst, uint32_t count);
bool sd_card_write(sd_card_t *card, uint32_t sector, const uint8_t *src, uint32_t count);
// ends an open multiple sector transfer and waits for the card
bool sd_card_sync(sd_card_t *card);
// the capacity in sectors, 0 on an error
uint32_t sd_card_sectors(sd_card_t *card);
bool sd_card_cid(sd_card_t *card, uint8_t cid[16]);
static inline uint8_t sd_card_error(const sd_card_t *card) { return card->err; }
static inline uint8_t sd_card_error_data(const sd_card_t *card) { return card->err_data; }

#ifdef __cplusplus
}
#endif
