#include "main.h"
#include "tusb.h"
#include "diag/hang_diag.h"
#include "at_cmd.h"
#include "file_name.h"
#include "kg_keys.h"

// USB CDC serial <-> core UART bridge (TS ZiFi and ZX Evo RS232). While a USB-UART adapter
// is plugged into the USB host (periph/usb_uart), the core UART goes to it instead, at the
// speed of the core, and the CDC port takes only the AT+KARABAS lines (the log stays there).
// AT+KARABAS=<cmd> lines from the host are not forwarded, the firmware answers them:
//   list - the installed cores: "+KARABAS:<file>;<id>;<name>;<build>" lines, then OK
//   boot - reboot into the RP2040 USB bootloader (BOOTSEL, firmware update)
//   msc  - reboot with the SD card exposed as USB mass storage (core update)
//   msc2 - the same with the second SD card (the slot of the FPGA, through the boot core)
//   esp  - BOOT core only: the bridge goes to the ESP8266 uart (AT commands to the module; a CR,
//          LF or CR LF of the host goes as CR LF)
//   uart - back to the core UART (also when another core is loaded)
//   sd2test - BOOT core only: reads the second SD card at a few SPI clocks (storage/sd2)
//   key:<k> [<k>...] - press and release keys, as a USB keyboard (to the core, OSD, hotkeys):
//          a key is modifiers and one key joined by '+' (lib/common/kg_keys.h: "ctrl+alt+del",
//          "menu+esc", "f3", "enter", "0x29"); 40 ms per report
//   type:<text> - type the printable chars of the text (US layout, Shift for the shifted ones)
//   hold:<k> - press a key and keep it down (until release or the next key / type)
//   release - all keys up

static uint8_t uart_idx = 0;
static uint8_t evo_rs232_dll = 0;
static uint8_t evo_rs232_dlm = 0;
static uint32_t serial_speed = 115200;
static unsigned long uart_rx_last = 0;
static uint32_t speed_changed = 0;         // ms_now() of the last speed change
// the speed goes to the adapter once it is still this long: EVO RS232 sends DLL and DLM apart
#define SPEED_SETTLE_MS 20

static kg_at_parser_t at_parser;
static char tx_buf[sizeof(at_parser.out)]; // bytes to forward to the core, one per rx period
static uint8_t tx_len = 0, tx_pos = 0;
static uint32_t at_hold_timer = 0;         // held bytes go to the core when the host goes quiet
#define AT_HOLD_TIMEOUT_MS 500
static bool esp_mode = false;
static kg_crlf_t esp_crlf;                 // the ESP8266 runs a command only after CR LF

// The host side is the CDC port (also the debug log, both from APP only). A write waits while
// the fifo is full, at most HOST_WRITE_WAIT_MS without progress (no terminal reading): USBD,
// above APP, sends meanwhile. Arduino's Serial.write waited for the whole write too.
#define HOST_WRITE_WAIT_MS 50

static void host_write(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *) data;
  uint32_t stalled = ms_now();
  while (len > 0 && tud_cdc_connected()) {
    uint32_t n = tud_cdc_write(p, len);
    if (n > 0) {
      p += n;
      len -= n;
      stalled = ms_now();
    } else {
      tud_cdc_write_flush();
      if (ms_since(stalled) >= HOST_WRITE_WAIT_MS) break;
    }
  }
}

static void host_print(const char *s) {
  host_write(s, strlen(s));
}

static void host_flush(void) {
  tud_cdc_write_flush();
}

void serial_set_speed(uint8_t dll, uint8_t dlm) {
  uint32_t speed = 0;
  if (dll == 0 && dlm == 0) {
    speed = 256000; // zx evo special case
  } else if (dlm & 0x80) {
    // native atmega mode
    dlm &= 0x7F;
    // (uint16)((DLM&0x7F)*256+DLL) = (691200/<скорость в бодах>)-1 
    speed = 691200 / ((dlm*256) + dll + 1);
  } else {
    // standard mode
    speed = 115200 / ((dlm*256) + dll);
  }
  // the pace of the bytes to the core (the CDC itself has no baud rate) and the adapter speed
  if (speed != serial_speed) {
    serial_speed = speed;
    speed_changed = ms_now();
  }
}

// a byte of the core: to the adapter if one is plugged, else to the CDC port
static void core_out(uint8_t data) {
  if (usb_uart_active()) usb_uart_write(data);
  else host_write(&data, 1);
}

void serial_data(uint8_t addr, uint8_t data) {
    if (addr == 0) {
      // ts zifi 115200
      if (serial_speed != 115200) { serial_speed = 115200; speed_changed = ms_now(); }
      core_out(data);
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
      core_out(data);
    }
}

static void at_list_cores() {
  static DIR root;
  if (!has_sd) { host_print("ERROR\r\n"); return; }
  if (!sd_opendir(&root, "/")) { host_print("ERROR\r\n"); return; }
  FILINFO fi;
  FIL *f;
  while (f_readdir(&root, &fi) == FR_OK && fi.fname[0]) {
    if (!(fi.fattrib & AM_DIR) && kg_name_has_ext(fi.fname, CORE_EXT) && (f = sd_tmp_open(fi.fname, FA_READ)) != NULL) {
      char id[33], name[33], build[9];
      file_read_str(f, FILE_POS_CORE_ID, id, 32);
      file_read_str(f, FILE_POS_CORE_NAME, name, 32);
      file_read_str(f, FILE_POS_CORE_BUILD, build, 8);
      char line[160];
      snprintf(line, sizeof(line), "+KARABAS:%s;%s;%s;%s\r\n", fi.fname, id, name, build);
      host_print(line);
      sd_tmp_close();
    }
  }
  sd_closedir(&root);
  host_print("OK\r\n");
}

// the reports of AT commands (sd2test) to the host
static void serial_out(const char *text) {
  host_print(text);
}

// into BOOTSEL: the saves first, the SD1 transfer ended (the card keeps it over the reboot)
static void reboot_bootsel(bool ok) {
  bk_flush();
  core_eeprom_flush();
  sd1_sync_before_reboot();
  if (ok) { host_print("OK\r\n"); host_flush(); }
  ms_delay(100);
  hang_diag_stop();
  matrix_icon(MATRIX_ICON_BOOTSEL); // the bootloader leaves the matrix as it is
  chip_reboot_bootsel();
}

// a report of keys down (mod / key) to the keyboard queue
static void key_report(uint8_t mod, uint8_t key) {
  hid_keyboard_report_t r = {0};
  r.modifier = mod;
  r.keycode[0] = key;
  hid_kbd_inject(&r);
}

// key:<keys>, type:<text>, hold:<key>: false when a key is unknown or the queue has no room
static bool at_keys(const char *arg) {
  if (strncasecmp(arg, "type:", 5) == 0) {
    const char *t = arg + 5;
    uint8_t mod, key;
    for (const char *c = t; *c; c++) if (!kg_key_ascii(*c, &mod, &key)) return false;
    if (hid_kbd_inject_free() < 2 * (int) strlen(t)) return false;
    for (const char *c = t; *c; c++) {
      kg_key_ascii(*c, &mod, &key);
      key_report(mod, key);
      key_report(0, 0);
    }
    return true;
  }
  bool hold = strncasecmp(arg, "hold:", 5) == 0;
  if (!hold && strncasecmp(arg, "key:", 4) != 0) return false;
  const char *s = arg + (hold ? 5 : 4);
  char name[KG_AT_MAX_ARG + 1];
  uint8_t mods[KG_AT_MAX_ARG], keys[KG_AT_MAX_ARG];
  int n = 0;
  while (*s) {
    while (*s == ' ') s++;
    if (!*s) break;
    size_t len = strcspn(s, " ");
    memcpy(name, s, len);
    name[len] = '\0';
    if (!kg_key_parse(name, &mods[n], &keys[n])) return false;
    n++;
    s += len;
  }
  if (n == 0 || (hold && n != 1) || hid_kbd_inject_free() < 2 * n) return false;
  for (int i = 0; i < n; i++) {
    key_report(mods[i], keys[i]);
    if (!hold) key_report(0, 0);
  }
  return true;
}

static void at_command(const char *arg) {
  d_printf("Serial: AT+KARABAS=%s", arg); d_println();
  if (strcasecmp(arg, "list") == 0) {
    at_list_cores();
  } else if (strcasecmp(arg, "version") == 0) {
    char line[64];
    snprintf(line, sizeof(line), "+KARABAS:%s;%u;%s;%s\r\n", STRINGIZE_VALUE_OF(BUILD_VER), HW_ID, core.id, core.build);
    host_print(line);
    host_print("OK\r\n");
  } else if (strcasecmp(arg, "boot") == 0) {
    reboot_bootsel(true);
  } else if (strcasecmp(arg, "msc") == 0 || strcasecmp(arg, "msc2") == 0) {
    host_print("OK\r\n"); host_flush(); ms_delay(100);
    hang_diag_stop();
    msc_reboot_to_msc(strcasecmp(arg, "msc2") == 0 ? 2 : 1);
  } else if (strcasecmp(arg, "esp") == 0 && core.type == CORE_TYPE_BOOT) {
    esp_mode = true;
    esp_crlf.cr = false;
    host_print("OK\r\n");
  } else if (strcasecmp(arg, "sd2test") == 0 && core.type == CORE_TYPE_BOOT) {
    sd2_test(serial_out);
  } else if (strcasecmp(arg, "uart") == 0) {
    esp_mode = false;
    host_print("OK\r\n");
  } else if (strcasecmp(arg, "release") == 0) {
    key_report(0, 0);
    host_print("OK\r\n");
  } else if (strncasecmp(arg, "key:", 4) == 0 || strncasecmp(arg, "type:", 5) == 0 ||
             strncasecmp(arg, "hold:", 5) == 0) {
    host_print(at_keys(arg) ? "OK\r\n" : "ERROR\r\n");
  } else {
    host_print("ERROR\r\n");
  }
}

void serial_bridge_begin() {
  kg_at_init(&at_parser);
}

// to the ESP8266 with the line ends of the host as CR LF (PuTTY sends a CR alone)
static void esp_write_host_bytes() {
  char out[2 * sizeof(at_parser.out)];
  esp_serial_write((const uint8_t *) out, kg_crlf(&esp_crlf, at_parser.out, at_parser.out_len, out));
}

// esp mode: host <-> ESP8266 uart, except while Setup Wi-Fi waits for a reply of the module
static void esp_tick() {
  if (esp_at_busy()) return;

  uint8_t buf[64];
  int n = 0;
  while (n < (int) sizeof(buf) && esp_serial_available() > 0) buf[n++] = (uint8_t) esp_serial_read();
  if (n > 0) { host_write(buf, n); host_flush(); }

  while (tud_cdc_available() > 0 && esp_serial_available_for_write() > 2 * (int) sizeof(at_parser.out)) {
    int c = tud_cdc_read_char();
    if (c == -1) return;
    at_hold_timer = ms_now();
    switch (kg_at_feed(&at_parser, (char) c)) {
      case KG_AT_PASS: esp_write_host_bytes(); break;
      case KG_AT_COMMAND: at_command(kg_at_arg(&at_parser)); return;
      default: break;
    }
  }
  if (at_parser.len > 0 && ms_since(at_hold_timer) >= AT_HOLD_TIMEOUT_MS && kg_at_flush(&at_parser) > 0) {
    esp_write_host_bytes();
  }
}

// with an adapter the CDC port takes only AT+KARABAS lines, the other bytes are dropped
static void host_commands() {
  while (tud_cdc_available() > 0) {
    int c = tud_cdc_read_char();
    if (c == -1) return;
    at_hold_timer = ms_now();
    if (kg_at_feed(&at_parser, (char) c) == KG_AT_COMMAND) { at_command(kg_at_arg(&at_parser)); return; }
  }
  if (at_parser.len > 0 && ms_since(at_hold_timer) >= AT_HOLD_TIMEOUT_MS) kg_at_flush(&at_parser);
}

// forward bytes received from the host (or the adapter) to the core
void serial_bridge_tick() {
  if (usb_device_bootsel_requested()) reboot_bootsel(false); // the 1200 baud touch
  // TinyUSB sends only full 64-byte packets by itself: the tail of what the core, the ESP or
  // at_command() wrote would wait for the next 64 bytes
  host_flush();
  if (esp_mode && core.type != CORE_TYPE_BOOT) esp_mode = false;
  if (esp_mode) { esp_tick(); return; }

  const bool adapter = usb_uart_active();
  if (adapter) {
    if (ms_since(speed_changed) >= SPEED_SETTLE_MS) usb_uart_set_baud(serial_speed);
    host_commands();
  }

  // the pace of the bytes to the core: 3 byte times on the CDC (the host sends as fast as it
  // can), the line rate from the adapter (its bytes come at that rate and would pile up)
  unsigned long uart_rx_delay = (adapter ? 10 : 30) * 1e6 / serial_speed;
  if (us_now() - uart_rx_last < uart_rx_delay) return;

  if (tx_pos >= tx_len) {
    tx_pos = tx_len = 0;
    if (adapter) {
      int c = usb_uart_read();
      if (c < 0) return;
      tx_buf[0] = (char) c;
      tx_len = 1;
    } else {
      if (tud_cdc_available() > 0) {
        int uart_rx = tud_cdc_read_char();
        if (uart_rx == -1) return;
        at_hold_timer = ms_now();
        switch (kg_at_feed(&at_parser, (char) uart_rx)) {
          case KG_AT_PASS: break;
          case KG_AT_COMMAND: at_command(kg_at_arg(&at_parser)); return;
          default: return;
        }
      } else if (!(at_parser.len > 0 && ms_since(at_hold_timer) >= AT_HOLD_TIMEOUT_MS && kg_at_flush(&at_parser) > 0)) {
        return;
      }
      memcpy(tx_buf, at_parser.out, at_parser.out_len);
      tx_len = at_parser.out_len;
    }
  }

  uart_idx++;
  uart_rx_last = us_now();
  spi_send(CMD_UART, uart_idx, (uint8_t) tx_buf[tx_pos++]);
}
