#include "sd_card.h"
#include "pico/time.h"

#define CMD0 0
#define CMD8 8
#define CMD9 9
#define CMD10 10
#define CMD12 12
#define CMD13 13
#define CMD17 17
#define CMD18 18
#define CMD24 24
#define CMD25 25
#define CMD55 55
#define CMD58 58
#define ACMD41 41

#define R1_READY 0x00
#define R1_IDLE 0x01
#define R1_ILLEGAL_COMMAND 0x04
#define TOKEN_START 0xFE        // a sector of a read, of a single write
#define TOKEN_WRITE_MULTI 0xFC
#define TOKEN_STOP 0xFD
#define DATA_RES_MASK 0x1F
#define DATA_RES_ACCEPTED 0x05

#define INIT_MS 2000
#define CMD_MS 300
#define READ_MS 300
#define WRITE_MS 600
#define INIT_HZ 400000

enum { STATE_IDLE, STATE_READ, STATE_WRITE };

static void cs(sd_card_t *c, bool selected) { c->bus->cs(c->bus->ctx, selected); }
static void send(sd_card_t *c, const uint8_t *b, size_t n) { c->bus->send(c->bus->ctx, b, n); }
static void send1(sd_card_t *c, uint8_t b) { c->bus->send(c->bus->ctx, &b, 1); }
static void recv(sd_card_t *c, uint8_t *b, size_t n) { c->bus->recv(c->bus->ctx, b, n); }

static uint8_t recv1(sd_card_t *c) {
  uint8_t b;
  c->bus->recv(c->bus->ctx, &b, 1);
  return b;
}

static bool error(sd_card_t *c, sd_err_t err, uint8_t data) {
  c->err = err;
  c->err_data = data;
  return false;
}

// a transfer: the bus, the chip select and one 0xFF after it goes low; at the end one 0xFF
// after it goes high (the card lets MISO go only with a clock), then the bus is free
static void start(sd_card_t *c) {
  c->bus->acquire(c->bus->ctx);
  cs(c, true);
  send1(c, 0xFF);
}

static void stop(sd_card_t *c) {
  cs(c, false);
  send1(c, 0xFF);
  c->bus->release(c->bus->ctx);
}

static bool wait_ready(sd_card_t *c, uint32_t ms) {
  uint32_t t0 = time_us_32();
  while (recv1(c) != 0xFF) {
    if (time_us_32() - t0 > ms * 1000) return false;
  }
  return true;
}

// the selected card: a command and its R1
static uint8_t command(sd_card_t *c, uint8_t cmd, uint32_t arg) {
  if (cmd != CMD0 && cmd != CMD12 && !wait_ready(c, CMD_MS)) return 0xFF;
  uint8_t buf[6] = { 0x40 | cmd, arg >> 24, arg >> 16, arg >> 8, arg, cmd == CMD0 ? 0x95 : 0x87 };
  send(c, buf, sizeof(buf));
  recv1(c); // the first fill byte (also the stuff byte after CMD12)
  uint8_t r1, n = 0;
  do {
    r1 = recv1(c);
  } while ((r1 & 0x80) && ++n < 10);
  return r1;
}

static uint8_t app_command(sd_card_t *c, uint8_t cmd, uint32_t arg) {
  command(c, CMD55, 0);
  return command(c, cmd, arg);
}

static uint32_t address(sd_card_t *c, uint32_t sector) {
  return c->type == SD_CARD_SDHC ? sector : sector << 9;
}

static bool read_data(sd_card_t *c, uint8_t *dst, size_t n) {
  uint32_t t0 = time_us_32();
  uint8_t token;
  while ((token = recv1(c)) == 0xFF) {
    if (time_us_32() - t0 > READ_MS * 1000) return error(c, SD_ERR_READ_TIMEOUT, token);
  }
  if (token != TOKEN_START) return error(c, SD_ERR_READ_TOKEN, token);
  recv(c, dst, n);
  uint8_t crc[2];
  recv(c, crc, 2);
  return true;
}

static bool write_data(sd_card_t *c, uint8_t token, const uint8_t *src) {
  static const uint8_t crc[2] = { 0xFF, 0xFF };
  send1(c, token);
  send(c, src, 512);
  send(c, crc, 2);
  uint8_t res = recv1(c);
  if ((res & DATA_RES_MASK) != DATA_RES_ACCEPTED) return error(c, SD_ERR_WRITE_DATA, res);
  return true;
}

bool sd_card_sync(sd_card_t *c) {
  bool ok = true;
  if (c->state == STATE_READ) {
    c->state = STATE_IDLE;
    uint8_t r1 = command(c, CMD12, 0);
    if (r1) ok = error(c, SD_ERR_CMD12, r1);
    stop(c);
  } else if (c->state == STATE_WRITE) {
    c->state = STATE_IDLE;
    if (wait_ready(c, WRITE_MS)) {
      send1(c, TOKEN_STOP);
    } else {
      ok = error(c, SD_ERR_WRITE_STOP, 0);
    }
    stop(c);
  }
  return ok;
}

bool sd_card_begin(sd_card_t *c, const sd_spi_bus_t *bus, uint32_t hz) {
  c->bus = bus;
  c->type = SD_CARD_NONE;
  c->state = STATE_IDLE;
  c->err = SD_ERR_NONE;
  c->err_data = 0;
  c->hz = hz;
  bus->clock(bus->ctx, INIT_HZ);
  // at least 74 clocks with the chip select high
  uint8_t ff[10];
  for (int i = 0; i < 10; i++) ff[i] = 0xFF;
  bus->acquire(bus->ctx);
  cs(c, false);
  send(c, ff, sizeof(ff));
  cs(c, true);
  send1(c, 0xFF);
  uint32_t t0 = time_us_32();
  sd_card_type_t type;
  uint8_t r1;
  while ((r1 = command(c, CMD0, 0)) != R1_IDLE) {
    if (time_us_32() - t0 > INIT_MS * 1000) goto fail_cmd0;
  }
  for (;;) {
    if (command(c, CMD8, 0x1AA) & R1_ILLEGAL_COMMAND) {
      type = SD_CARD_SD1;
      break;
    }
    uint8_t r7[4];
    recv(c, r7, 4);
    if (r7[3] == 0xAA) {
      type = SD_CARD_SD2;
      break;
    }
    if (time_us_32() - t0 > INIT_MS * 1000) {
      error(c, SD_ERR_CMD8, r7[3]);
      goto fail;
    }
  }
  while ((r1 = app_command(c, ACMD41, type == SD_CARD_SD2 ? 0x40000000 : 0)) != R1_READY) {
    if (time_us_32() - t0 > INIT_MS * 1000) {
      error(c, SD_ERR_ACMD41, r1);
      goto fail;
    }
  }
  if (type == SD_CARD_SD2) {
    if ((r1 = command(c, CMD58, 0))) {
      error(c, SD_ERR_CMD58, r1);
      goto fail;
    }
    uint8_t ocr[4];
    recv(c, ocr, 4);
    if ((ocr[0] & 0xC0) == 0xC0) type = SD_CARD_SDHC;
  }
  stop(c);
  c->type = type;
  bus->clock(bus->ctx, hz);
  return true;
fail_cmd0:
  error(c, SD_ERR_CMD0, r1);
fail:
  stop(c);
  return false;
}

bool sd_card_read(sd_card_t *c, uint32_t sector, uint8_t *dst, uint32_t count) {
  if (!c->type) return false;
  uint8_t r1;
  if (count == 1 && !c->bus->dedicated) {
    if (!sd_card_sync(c)) return false;
    start(c);
    if ((r1 = command(c, CMD17, address(c, sector)))) {
      error(c, SD_ERR_CMD17, r1);
      stop(c);
      return false;
    }
    bool ok = read_data(c, dst, 512);
    stop(c);
    return ok;
  }
  if (c->state != STATE_READ || c->cur_sector != sector) {
    if (!sd_card_sync(c)) return false;
    start(c);
    if ((r1 = command(c, CMD18, address(c, sector)))) {
      error(c, SD_ERR_CMD18, r1);
      stop(c);
      return false;
    }
    c->state = STATE_READ;
    c->cur_sector = sector;
  }
  for (uint32_t i = 0; i < count; i++, dst += 512) {
    if (!read_data(c, dst, 512)) {
      // the transfer is broken: end it
      uint8_t err = c->err, data = c->err_data;
      sd_card_sync(c);
      return error(c, err, data);
    }
  }
  c->cur_sector += count;
  return c->bus->dedicated ? true : sd_card_sync(c);
}

bool sd_card_write(sd_card_t *c, uint32_t sector, const uint8_t *src, uint32_t count) {
  if (!c->type) return false;
  uint8_t r1;
  if (count == 1 && !c->bus->dedicated) {
    if (!sd_card_sync(c)) return false;
    start(c);
    bool ok = true;
    if ((r1 = command(c, CMD24, address(c, sector)))) {
      ok = error(c, SD_ERR_CMD24, r1);
    } else if (!write_data(c, TOKEN_START, src)) {
      ok = false;
    } else if (!wait_ready(c, WRITE_MS)) {
      ok = error(c, SD_ERR_WRITE_TIMEOUT, 0);
    } else if ((r1 = command(c, CMD13, 0)) || (r1 = recv1(c))) {
      ok = error(c, SD_ERR_CMD13, r1);
    }
    stop(c);
    return ok;
  }
  if (c->state != STATE_WRITE || c->cur_sector != sector) {
    if (!sd_card_sync(c)) return false;
    start(c);
    if ((r1 = command(c, CMD25, address(c, sector)))) {
      error(c, SD_ERR_CMD25, r1);
      stop(c);
      return false;
    }
    c->state = STATE_WRITE;
    c->cur_sector = sector;
  }
  for (uint32_t i = 0; i < count; i++, src += 512) {
    if (!wait_ready(c, WRITE_MS)) {
      error(c, SD_ERR_WRITE_TIMEOUT, 0);
      c->state = STATE_IDLE;
      stop(c);
      return false;
    }
    if (!write_data(c, TOKEN_WRITE_MULTI, src)) {
      uint8_t err = c->err, data = c->err_data;
      sd_card_sync(c);
      return error(c, err, data);
    }
  }
  c->cur_sector += count;
  return c->bus->dedicated ? true : sd_card_sync(c);
}

static bool read_register(sd_card_t *c, uint8_t cmd, uint8_t reg[16], sd_err_t err) {
  if (!c->type || !sd_card_sync(c)) return false;
  start(c);
  uint8_t r1 = command(c, cmd, 0);
  bool ok = r1 ? error(c, err, r1) : read_data(c, reg, 16);
  stop(c);
  return ok;
}

uint32_t sd_card_sectors(sd_card_t *c) {
  uint8_t csd[16];
  if (!read_register(c, CMD9, csd, SD_ERR_CMD9)) return 0;
  switch (csd[0] >> 6) {
    case 0: { // CSD v1
      uint32_t c_size = ((uint32_t) (csd[6] & 3) << 10) | ((uint32_t) csd[7] << 2) | (csd[8] >> 6);
      uint8_t mult = ((csd[9] & 3) << 1) | (csd[10] >> 7);
      uint8_t bl_len = csd[5] & 15;
      return (c_size + 1) << (mult + bl_len + 2 - 9);
    }
    case 1: { // CSD v2
      uint32_t c_size = ((uint32_t) (csd[7] & 0x3F) << 16) | ((uint32_t) csd[8] << 8) | csd[9];
      return (c_size + 1) << 10;
    }
    default:
      error(c, SD_ERR_CSD, csd[0]);
      return 0;
  }
}

bool sd_card_cid(sd_card_t *c, uint8_t cid[16]) {
  return read_register(c, CMD10, cid, SD_ERR_CMD10);
}
