#include "main.h"

// FPGA slave serial configuration (bit-banged DIN + CCLK)

uint32_t fpga_send(const char* filename) {

  d_print("Configuring FPGA by "); d_println(filename);

  sd1.chvol();
  if (!file1.open(filename, FILE_READ)) {
    halt("Unable to open bitstream file to read");
  }

  // get bitstream size
  uint32_t length = file_read32(FILE_POS_BITSTREAM_LEN);
  d_print("Bitstream size: "); d_println(length, DEC);

  // seek to bitstream start
  file_seek(FILE_POS_BITSTREAM_START);

  pinMode(PIN_CONF_CLK, OUTPUT);
  pinMode(PIN_CONF_IO1, OUTPUT);

  // pulse PROG_B
  digitalWrite(PIN_CONF_PRG_B, HIGH);
  digitalWrite(PIN_CONF_PRG_B, LOW);
  digitalWrite(PIN_CONF_PRG_B, HIGH);

  // wait for INIT_B = 0
  delay(10);

  ElapsedTimer timer;
  timer.reset();

  uint32_t total = length;
  uint32_t i = 0;
  bool blink = false;
  char line[128];
  int n;

  digitalWrite(PIN_CONF_CLK, LOW);

  while (length > 0 && (n = file_read_buf(line, (sizeof(line) < length ? sizeof(line) : length) )) > 0) {
    i += n;
    length -=n;

    for (int s=0; s<n; s++) {
      uint8_t c = line[s];
      for (uint8_t j=0; j<8; ++j) {
        // Set bit of data
        gpio_put(PIN_CONF_IO1, (c & (1<<(7-j))) ? HIGH : LOW);
        // Latch bit of data by CCLK impulse
        gpio_put(PIN_CONF_CLK, HIGH);
        gpio_put(PIN_CONF_CLK, LOW);
      }
    }

    if ((i % 8192 == 0) || (length == 0)) {
      blink = !blink;
      led_write(0, blink);
      led_write(1, blink);
      matrix_progress(i, total);
    }
  }
  file1.close();

  pinMode(PIN_CONF_CLK, INPUT);
  pinMode(PIN_CONF_IO1, INPUT);

  d_print(i, DEC); d_println(" bytes done");
  d_print("Elapsed time: "); d_print(timer.elapsed(), DEC); d_println(" ms");
  d_flush();

  if (i < total) {
    d_print("Bitstream is truncated, expected "); d_print(total, DEC); d_println(" bytes");
  }

  d_print("Waiting for CONF_DONE... ");
  timer.reset();
  while(digitalRead(PIN_CONF_DONE) == LOW) {
    if (timer.elapsed() >= 1000) {
      halt("FPGA configuration failed: CONF_DONE timeout");
    }
    delay(10);
  }
  d_println("Done");

  return i;
}
