#include "main.h"
#include "at_cmd.h"
#include "file_name.h"

// USB CDC serial <-> core UART bridge (TS ZiFi and ZX Evo RS232).
// AT+KARABAS=<cmd> lines from the host are not forwarded, the firmware answers them:
//   list - the installed cores: "+KARABAS:<file>;<id>;<name>;<build>" lines, then OK
//   boot - reboot into the RP2040 USB bootloader (BOOTSEL, firmware update)
//   msc  - reboot with the SD card exposed as USB mass storage (core update)

static uint8_t uart_idx = 0;
static uint8_t evo_rs232_dll = 0;
static uint8_t evo_rs232_dlm = 0;
static uint32_t serial_speed = 115200;
static unsigned long uart_rx_last = 0;

static kg_at_parser_t at_parser;
static char tx_buf[sizeof(at_parser.out)]; // bytes to forward to the core, one per rx period
static uint8_t tx_len = 0, tx_pos = 0;
static ElapsedTimer at_hold_timer;         // held bytes go to the core when the host goes quiet
#define AT_HOLD_TIMEOUT_MS 500

void serial_set_speed(uint8_t dll, uint8_t dlm) {
  uint32_t speed = 0;
  if (dll == 0 && dlm == 0) {
    speed = 256000; // zx evo special case
  } else if (bitRead(dlm, 7) == 1) {
    // native atmega mode
    dlm = bitClear(dlm, 7);
    // (uint16)((DLM&0x7F)*256+DLL) = (691200/<скорость в бодах>)-1 
    speed = 691200 / ((dlm*256) + dll + 1);
  } else {
    // standard mode
    speed = 115200 / ((dlm*256) + dll);
  }
  // switch serial spseed
  if (serial_speed != speed) {
    serial_speed = speed;
    Serial.end();
    Serial.begin(speed);
  }
}

void serial_data(uint8_t addr, uint8_t data) {
    if (addr == 0) {
      // ts zifi 115200
      if (serial_speed != 115200) {
        serial_speed = 115200;
        Serial.end();
        Serial.begin(serial_speed);
      }
      Serial.write(data);
    } else if (addr == 1) {
      // evo rs232 dll
      evo_rs232_dll = data;
      serial_set_speed(evo_rs232_dll, evo_rs232_dlm);
    } else if (addr == 2) {
      // evo rs232 set dlm
      evo_rs232_dlm = data;
      serial_set_speed(evo_rs232_dll, evo_rs232_dlm);
    } else if (addr == 3) {
      // evo rs232 data
      Serial.write(data);
    }
}

// header string of a core file without the padding
static void read_core_str(FsFile &f, uint32_t pos, char *out, size_t len) {
  f.seekSet(pos);
  int n = f.read(out, len);
  out[n > 0 ? n : 0] = '\0';
  for (int i = strlen(out) - 1; i >= 0 && (out[i] == ' ' || out[i] == '\0'); i--) out[i] = '\0';
}

static void at_list_cores() {
  if (!has_sd) { Serial.print("ERROR\r\n"); return; }
  FsFile root, f;
  if (!root.open("/", O_RDONLY)) { Serial.print("ERROR\r\n"); return; }
  while (f.openNext(&root, O_RDONLY)) {
    char filename[64]; f.getName(filename, sizeof(filename));
    if (!f.isDir() && kg_name_has_ext(filename, CORE_EXT)) {
      char id[33], name[33], build[9];
      read_core_str(f, FILE_POS_CORE_ID, id, 32);
      read_core_str(f, FILE_POS_CORE_NAME, name, 32);
      read_core_str(f, FILE_POS_CORE_BUILD, build, 8);
      Serial.printf("+KARABAS:%s;%s;%s;%s\r\n", filename, id, name, build);
    }
    f.close();
  }
  root.close();
  Serial.print("OK\r\n");
}

static void at_command(const char *arg) {
  d_printf("Serial: AT+KARABAS=%s", arg); d_println();
  if (strcasecmp(arg, "list") == 0) {
    at_list_cores();
  } else if (strcasecmp(arg, "boot") == 0) {
    Serial.print("OK\r\n"); Serial.flush(); delay(100);
    rp2040.rebootToBootloader();
  } else if (strcasecmp(arg, "msc") == 0) {
    Serial.print("OK\r\n"); Serial.flush(); delay(100);
    msc_reboot_to_msc();
  } else {
    Serial.print("ERROR\r\n");
  }
}

void serial_bridge_begin() {
  kg_at_init(&at_parser);
  Serial.begin(serial_speed);
}

// forward bytes received from the host to the core
void serial_bridge_tick() {
  // receive at least on the serial_speed
  unsigned long uart_rx_delay = 30 * 1e6 / serial_speed;
  if (micros() - uart_rx_last < uart_rx_delay) return;

  if (tx_pos >= tx_len) {
    tx_pos = tx_len = 0;
    if (Serial.available() > 0) {
      int uart_rx = Serial.read();
      if (uart_rx == -1) return;
      at_hold_timer.reset();
      switch (kg_at_feed(&at_parser, (char) uart_rx)) {
        case KG_AT_PASS: break;
        case KG_AT_COMMAND: at_command(kg_at_arg(&at_parser)); return;
        default: return;
      }
    } else if (!(at_parser.len > 0 && at_hold_timer.elapsed() >= AT_HOLD_TIMEOUT_MS && kg_at_flush(&at_parser) > 0)) {
      return;
    }
    memcpy(tx_buf, at_parser.out, at_parser.out_len);
    tx_len = at_parser.out_len;
  }

  uart_idx++;
  uart_rx_last = micros();
  spi_send(CMD_UART, uart_idx, (uint8_t) tx_buf[tx_pos++]);
}
