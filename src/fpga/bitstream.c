#include "main.h"
#include "diag/hang_diag.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "fpga_serial.pio.h"

// FPGA slave serial configuration (DIN + CCLK).
// The bitstream is streamed by a PIO state machine fed by DMA, while the next chunk
// is read from the SD card. Falls back to bit-banging if no PIO SM / DMA channel is free.

#ifndef FPGA_CCLK_HZ
#define FPGA_CCLK_HZ 20000000 // CCLK frequency for the PIO loader
#endif

#define BITSTREAM_CHUNK 4096

static uint8_t chunk_buf[2][BITSTREAM_CHUNK];

static void bitstream_progress(uint32_t sent, uint32_t total) {
  static bool blink = false;
  if ((sent % 8192 == 0) || (sent == total)) {
    blink = !blink;
    led_write(0, blink);
    led_write(1, blink);
    matrix_progress(sent, total);
  }
}

static int bitstream_read(uint8_t *buf, uint32_t remaining) {
  return file_read_buf((char*) buf, (remaining < BITSTREAM_CHUNK) ? remaining : BITSTREAM_CHUNK);
}

// PIO-USB claims DMA channel 0 on core1, so take channels from the top
static int claim_dma_channel() {
  for (int ch = NUM_DMA_CHANNELS - 1; ch > 0; ch--) {
    if (!dma_channel_is_claimed(ch)) {
      dma_channel_claim(ch);
      return ch;
    }
  }
  return -1;
}

static bool bitstream_send_pio(uint32_t total, uint32_t *sent) {
  // let core1 allocate its PIO-USB programs and DMA channel first
  uint32_t wait = ms_now();
  while (!usb_host_ready && ms_since(wait) < 500) { ms_delay(1); }

  // the program and a state machine for this load only (pio1 belongs to the USB host)
  PIO pio;
  uint sm, offset;
  uint pin_lo = MIN(PIN_CONF_IO1, PIN_CONF_CLK);
  uint pin_cnt = MAX(PIN_CONF_IO1, PIN_CONF_CLK) - pin_lo + 1;
  if (!pio_claim_free_sm_and_add_program_for_gpio_range(&fpga_serial_program, &pio, &sm, &offset, pin_lo, pin_cnt, true)) {
    d_println("No free PIO state machine for the bitstream loader");
    return false;
  }
  int dma = claim_dma_channel();
  if (dma < 0) {
    d_println("No free DMA channel for the bitstream loader");
    pio_remove_program_and_unclaim_sm(&fpga_serial_program, pio, sm, offset);
    return false;
  }

  float clkdiv = (float) clock_get_hz(clk_sys) / (2.0f * FPGA_CCLK_HZ); // 2 PIO instructions per bit
  if (clkdiv < 1.0f) clkdiv = 1.0f;
  fpga_serial_program_init(pio, sm, offset, PIN_CONF_IO1, PIN_CONF_CLK, clkdiv);
  pio_sm_set_enabled(pio, sm, true);

  dma_channel_config c = dma_channel_get_default_config(dma);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));

  // double buffering: DMA streams one chunk while the next one is read from SD
  uint8_t cur = 0;
  int n = bitstream_read(chunk_buf[cur], total - (*sent));
  while (n > 0) {
    dma_channel_configure(dma, &c, &pio->txf[sm], chunk_buf[cur], n, true);
    (*sent) += n;
    bitstream_progress((*sent), total);
    cur ^= 1;
    int next = ((*sent) < total) ? bitstream_read(chunk_buf[cur], total - (*sent)) : 0;
    dma_channel_wait_for_finish_blocking(dma);
    n = next;
  }

  // wait for the last byte to be shifted out of the OSR
  while (!pio_sm_is_tx_fifo_empty(pio, sm)) { tight_loop_contents(); }
  busy_wait_us_32(10);

  pio_sm_set_enabled(pio, sm, false);
  pio_remove_program_and_unclaim_sm(&fpga_serial_program, pio, sm, offset);
  dma_channel_unclaim(dma);
  return true;
}

static void bitstream_send_bitbang(uint32_t total, uint32_t *sent) {
  int n;
  while ((*sent) < total && (n = bitstream_read(chunk_buf[0], total - (*sent))) > 0) {
    for (int s=0; s<n; s++) {
      uint8_t c = chunk_buf[0][s];
      for (uint8_t j=0; j<8; ++j) {
        // Set bit of data
        gpio_put(PIN_CONF_IO1, (c & (1<<(7-j))) ? 1 : 0);
        // Latch bit of data by CCLK impulse
        gpio_put(PIN_CONF_CLK, 1);
        gpio_put(PIN_CONF_CLK, 0);
      }
    }
    (*sent) += n;
    bitstream_progress((*sent), total);
  }
}

uint32_t fpga_send(const char* filename) {

  d_print("Configuring FPGA by "); d_println(filename);

  if (!sd_open(&file1, filename, FA_READ)) {
    halt("Unable to open bitstream file to read");
  }

  // get bitstream size
  uint32_t total = file_read32(FILE_POS_BITSTREAM_LEN);
  d_printf("Bitstream size: %lu", (unsigned long) total); d_println();

  // seek to bitstream start
  file_seek(FILE_POS_BITSTREAM_START);

  pin_output(PIN_CONF_CLK);
  pin_output(PIN_CONF_IO1);
  gpio_put(PIN_CONF_CLK, 0);

  // pulse PROG_B: low for at least T_PROGRAM (500 ns on Spartan-6). Arduino's digitalWrite
  // calls made the pulse long enough by themselves; gpio_put back to back is a few cycles,
  // and the fpga then came up half reset (the OSD flags of the next core were ignored)
  gpio_put(PIN_CONF_PRG_B, 1);
  gpio_put(PIN_CONF_PRG_B, 0);
  busy_wait_us_32(5);
  gpio_put(PIN_CONF_PRG_B, 1);

  // wait for INIT_B = 0
  ms_delay(10);
  bool init_ready = gpio_get(PIN_CONF_INIT_B); // the fpga has cleared its memory

  uint32_t timer = ms_now();
  timer = ms_now();

  uint32_t sent = 0;
  hang_step(0x90);
  if (!bitstream_send_pio(total, &sent)) {
    d_println("Falling back to bit-banged bitstream loading");
    bitstream_send_bitbang(total, &sent);
  }
  hang_step(0x91);
  sd_close(&file1);

  // give the pins back to SIO
  pin_input(PIN_CONF_CLK);
  pin_input(PIN_CONF_IO1);

  hang_step(0x92);
  d_printf("%lu bytes done", (unsigned long) sent); d_println();
  d_printf("Elapsed time: %lu ms", (unsigned long) ms_since(timer)); d_println();
  d_flush();
  hang_step(0x93);

  if (sent < total) {
    d_printf("Bitstream is truncated, expected %lu bytes", (unsigned long) total); d_println();
  }

  d_print("Waiting for CONF_DONE... ");
  timer = ms_now();
  hang_step(0x94);
  while(!gpio_get(PIN_CONF_DONE)) {
    if (ms_since(timer) >= 1000) {
      // INIT_B low after the bitstream: a CRC error
      halt(!init_ready ? "FPGA configuration failed: CONF_DONE timeout, INIT_B low before the bitstream" :
           sent < total ? "FPGA configuration failed: CONF_DONE timeout, bitstream truncated" :
           gpio_get(PIN_CONF_INIT_B) ? "FPGA configuration failed: CONF_DONE timeout, INIT_B high" :
           "FPGA configuration failed: CONF_DONE timeout, INIT_B low (CRC error)");
    }
    ms_delay(10);
  }
  hang_step(0x95);
  d_println("Done");

  return sent;
}
