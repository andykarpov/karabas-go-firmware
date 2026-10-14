#include "main.h"
#include "osd_font.h"
#include "ps2kbd.h"
#include "usb_hid_keys.h"
#include "kg_format.h"

// Loading a core file: bitstream, header (OSD items, eeprom, switches), ROMs.

core_item_t core;
core_file_slot_t file_slots[MAX_FILE_SLOTS];
static bool is_flashboot = false;

void core_send(uint8_t pos)
{
  if (core.osd[pos].type == CORE_OSD_TYPE_FILEMOUNTER) {
      // todo
      //spi_send(CMD_SWITCHES, pos, (file_slots[core.osd[pos].slot_id].is_mounted) ? 1 : 0);
      //d_printf("File mounter: %s %d", core.osd[pos].name, file_slots[core.osd[pos].slot_id].is_mounted);
  } else if (core.osd[pos].type == CORE_OSD_TYPE_FILELOADER) {
    // do nothing
  } else {
    spi_send(CMD_SWITCHES, pos, core.osd[pos].val);
    d_printf("Switch: %s %d", core.osd[pos].name, core.osd[pos].val);
  }
  d_println();
}

void core_send_all() 
{
  for (uint8_t i=0; i<core.osd_len; i++) {
    if (core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER) {
      // todo
      //spi_send(CMD_SWITCHES, i, (file_slots[core.osd[i].slot_id].is_mounted) ? 1 : 0);
    } else if (core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
      // do nothing
    } else {
      spi_send(CMD_SWITCHES, i, core.osd[i].val);
    }
  }
}

void core_trigger(uint8_t pos)
{
  d_printf("Trigger: %s", core.osd[pos].name);
  d_println();

  // reset FT812 on soft reset
  // todo: replace hardcode with something else
  if (memcmp(core.osd[pos].name, "Rese", 4) == 0) {
    d_println("Reset FT812");
    ft.reset();
    // reset ESP and MIDI on soft reset (mini revG)
    board_reset_peripherals();
    delay(100);
  }

  spi_send(CMD_SWITCHES, pos, 1);
  delay(100);
  spi_send(CMD_SWITCHES, pos, 0);
}

uint8_t core_eeprom_get(uint8_t pos) {
  return core.eeprom[pos].val;
}

void core_eeprom_set(uint8_t pos, uint8_t val) {
  core.eeprom[pos].val = val;
  core.eeprom_need_save = true;
}

void flashboot (uint8_t data) {
  if (is_flashboot) return; // skip already in flashboot mode
  uint8_t flashboot_coreid = data;
  d_printf("Flashboot core id: %02x", data); d_println();
  d_flush(); delay(100);
  // todo: add flashboot_id to the core structure, search for desired id and boot it
  // now it's implemented as hack: reload the same core (required for zx next hard reset)
  // d_printf("Core id %s", core.id); d_println();
  // d_printf("Core filename %s", core.filename); d_println();
  //String f = String(core.filename); f.trim(); 
  //d_printf("Loading core %s", f); d_println();
  //char buf[33]; f.toCharArray(buf, sizeof(buf));
  String id = String(core.id); id.trim();
  if (id.compareTo("zxnext") == 0) { // allow flashboot only for zxnext core
    is_flashboot = true;
    do_configure(core.filename);
  }
}

void do_configure(const char* filename) {
  matrix_on_configure_start();
  link_set_rx_enabled(false);
  ft.vga(false); // FT off
  ft.spi(false);
  kb_reset(); // reset to ps/2 defaults

  fpga_send(filename);
  spi_send(CMD_INIT_START, 0, 0);
  spi_send(CMD_HW_SETUP, 0, HW_ID); // hw id
  spi_send(CMD_HW_SETUP, 1, hw_setup.dvi_only); // dvi only flag
  // trigger font loader reset
  zxosd.fontReset();
  // send font data
  for (int i=0; i<OSD_FONT_SIZE; i++) {
    zxosd.fontSend(osd_font[i]);
  }
  read_core(filename);
  if (!is_osd) {
    zxosd.clear();
    zxosd.logo(0,0, HW_ID);
    zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
    zxosd.setPos(0,5);
    zxosd.print("ROM ");
    zxosd.update();
    zxosd.showPopup();
  }
  read_roms(filename);
  if (!is_osd) {
    zxosd.hidePopup();
    osd_handle(true); // reinit osd
  }
  for (uint8_t i=0; i<7; i++) { // cleanup kbd: modifier + 6 keycodes
    spi_queue(CMD_USB_KBD, i, 0);
  }
  spi_send(CMD_INIT_DONE, 0, 0);
  is_flashboot = false;
  link_set_rx_enabled(true);

  // reinit matrix
  matrix_on_configure_done(core.type);
}

void read_core(const char* filename) {

  sd1.chvol();
  if (!file1.open(filename, FILE_READ)) {
    halt("Unable to open bitstream file to read");
  }

  core.flash = false;
  file1.getName(core.filename, sizeof(core.filename));
  core.filename[32] = '\0';
  file_seek(FILE_POS_CORE_NAME); file_read_bytes(core.name, 32); core.name[32] = '\0';
  uint8_t visible; file_seek(FILE_POS_CORE_VISIBLE); visible = file_read(); core.visible = (visible > 0);
  file_seek(FILE_POS_CORE_ORDER); core.order = file_read();
  file_seek(FILE_POS_CORE_TYPE); core.type = file_read();
  // show OSD on boot (only for boot and fileloader cores)
  is_osd = false;
  switch (core.type) {
    case CORE_TYPE_BOOT: is_osd = true; break;
    case CORE_TYPE_OSD: is_osd = false; break;
    case CORE_TYPE_FILELOADER: is_osd = true; break;
    default: is_osd = false;
  }  
  d_print("Core type: ");
  switch (core.type) {
    case CORE_TYPE_BOOT: d_println("Boot"); break;
    case CORE_TYPE_OSD: d_println("Normal"); break;
    case CORE_TYPE_FILELOADER: d_println("Fileloader"); break;
    case CORE_TYPE_HIDDEN: d_println("Hidden"); break;
    default: d_println("Reserved");
  }
  core.bitstream_length = file_read32(FILE_POS_BITSTREAM_LEN);
  file_seek(FILE_POS_CORE_ID); file_read_bytes(core.id, 32); core.id[32] = '\0';
  file_seek(FILE_POS_CORE_BUILD); file_read_bytes(core.build, 8); core.build[8] = '\0';
  file_seek(FILE_POS_CORE_EEPROM_BANK); core.eeprom_bank = file_read();
  file_seek(FILE_POS_RTC_TYPE); core.rtc_type = file_read();
  file_seek(FILE_POS_FILELOADER_DIR); file_read_bytes(core.dir, 32); core.dir[32] = '\0';
  file_seek(FILE_POS_FILELOADER_FILE); core.last_file_id = file_read16(FILE_POS_FILELOADER_FILE);
  file_seek(FILE_POS_FILELOADER_EXTENSIONS); file_read_bytes(core.file_extensions, 32); core.file_extensions[32] = '\0';
  file_seek(FILE_POS_SPI_FREQ); core.spi_freq = file_read();
  link_set_freq(core.spi_freq);
  uint32_t roms_len = file_read32(FILE_POS_ROM_LEN);
  uint32_t offset = FILE_POS_BITSTREAM_START + core.bitstream_length + roms_len;
  //d_print("OSD section: "); d_println(offset);
  file_seek(offset); core.osd_len = file_read();
  //d_print("OSD len: "); d_println(core.osd_len);

  d_print("Core SPI Frequency: "); if (core.spi_freq > 0 && core.spi_freq < 255) { d_print(core.spi_freq); d_println(" MHz"); } else d_println("default");
  
  for (uint8_t i=0; i<MAX_FILE_SLOTS; i++) { file_slots[i].is_mounted = false; }

  for (uint8_t i=0; i<core.osd_len; i++) {
    core.osd[i].type = file_read();
    file_read();
    file_read_bytes(core.osd[i].name, 16); core.osd[i].name[16] = '\0';
    core.osd[i].def = file_read();
    core.osd[i].val = core.osd[i].def;
    core.osd[i].prev_val = core.osd[i].def;

    // filemounter osd type:
    // loading initial dir, filename, extensions and trying to mount file, if any
    if (core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[i].type == CORE_OSD_TYPE_FILELOADER) {
      core.osd[i].options_len = 0;
      core.osd[i].slot_id = file_read();
      file_slots[core.osd[i].slot_id].is_autoload = bitRead(core.osd[i].slot_id, 7);
      core.osd[i].slot_id = bitClear(core.osd[i].slot_id, 7);
      file_slots[core.osd[i].slot_id].is_mounted = false;
      file_read_bytes(file_slots[core.osd[i].slot_id].ext, 256); file_slots[core.osd[i].slot_id].ext[255] = '\0';
      file_slots[core.osd[i].slot_id].offset_dir = file1.curPosition();
      file_read_bytes(file_slots[core.osd[i].slot_id].dir, 256); file_slots[core.osd[i].slot_id].dir[255] = '\0';
      String dir = String(file_slots[core.osd[i].slot_id].dir);
      if (dir == "") { dir = "/"; }
      if (dir.charAt(0) != '/') { dir = '/' + dir; }
      dir.toCharArray(file_slots[core.osd[i].slot_id].dir, sizeof(file_slots[core.osd[i].slot_id].dir));
      file_slots[core.osd[i].slot_id].offset_filename = file1.curPosition();
      file_read_bytes(file_slots[core.osd[i].slot_id].filename, 256); file_slots[core.osd[i].slot_id].filename[255] = '\0';
      String sfilename = String( file_slots[core.osd[i].slot_id].filename); sfilename.trim();
      String sfullname = dir + "/" + sfilename; sfullname.trim();
      if (sfilename.length() > 0 && sd1.exists(sfullname)) {
        file_slots[core.osd[i].slot_id].is_mounted = true; //file_slots[core.osd[i].slot_id].file = sd1.open(sfullname, O_READ);
        if (file_slots[core.osd[i].slot_id].is_autoload) {
          // todo: autoload (spi commands to the host)
          // todo: depends on type
        }
      }
    }
      // otherwise - reading options structure
      else {
      core.osd[i].options_len = file_read();
      if (core.osd[i].options_len > 8) {
        core.osd[i].options_len = 8; // something goes wrong
      } 
      for (uint8_t j=0; j<core.osd[i].options_len; j++) {
        file_read_bytes(core.osd[i].options[j].name, 16); core.osd[i].options[j].name[16] = '\0';
      }
    }
    file_read_bytes(core.osd[i].hotkey, 16); core.osd[i].hotkey[16] = '\0';
    core.osd[i].keys[0] = file_read();
    core.osd[i].keys[1] = file_read();
    file_read(); // reserved
    file_read(); 
    file_read();
  }

  // read eeprom data from file (in case rombank = 4 and up)
  // 255 means no eeprom allowed by core
  if (core.eeprom_bank >= MAX_EEPROM_BANKS && core.eeprom_bank != NO_EEPROM_BANK) {
    for (uint8_t i=0; i<255; i++) {
      file_seek(FILE_POS_EEPROM_DATA + i);
      core.eeprom[i].val = file_read();
      core.eeprom[i].prev_val = core.eeprom[i].val;
    }
  }
  zxrtc.setEepromBank(core.eeprom_bank);
  zxrtc.setRtcType(core.rtc_type);
  zxrtc.sendAll();

  // read saved switches
  for(uint8_t i=0; i<core.osd_len; i++) {
    file_seek(FILE_POS_SWITCHES_DATA + i);
    core.osd[i].val = file_read();
    if (core.osd[i].val > core.osd[i].options_len-1) {
      core.osd[i].val = core.osd[i].def;
    }
    core.osd[i].prev_val = core.osd[i].val;
  }
  core_send_all();

  // re-send rtc registers
  zxrtc.sendAll();

  has_ft = false;

  // hw ft reset for other cores types
  if (core.type != CORE_TYPE_BOOT) {
    ft.reset();
  }

  // reset ESP and MIDI (mini revG)
  board_reset_peripherals();

  // boot core tries to use FT812 as osd handler
  if (core.type == CORE_TYPE_BOOT && is_osd) {
    if (hw_setup.ft_enabled) {
      // space skip ft osd
      hid_kbd_report_sync();
      if (usb_keyboard_report.keycode[0] == KEY_SPACE) {
        d_println("Space pressed: skip FT81x detection, fallback to classic OSD");
      } else {
        ft.spi(true);
        has_ft = ft.init(hw_setup.ft_video_mode);
        if (has_ft) {
          d_println("Found FT81x IC, switching to FT OSD");
          ft.vga(true);
        } else {
          d_println("FT81x IC did not found");
          ft.vga(false);
          ft.spi(false);
        }
      }
    }
  }

  // dump parsed OSD items
  for(uint8_t i=0; i<core.osd_len; i++) {
    d_printf("OSD %d: type: %d name: %s def: %d len: %d keys: [%d %d %d]", i, core.osd[i].type, core.osd[i].name, core.osd[i].def, core.osd[i].options_len, core.osd[i].keys[0], core.osd[i].keys[1], core.osd[i].keys[2]); 
    d_println();
    for (uint8_t j=0; j<core.osd[i].options_len; j++) {
      d_print(core.osd[i].options[j].name); d_print(", "); 
    } 
    d_println();
    d_print(core.osd[i].hotkey); d_println();
  }

  file1.close();

  // mount slots
  for(uint8_t i=0; i<core.osd_len; i++) {
    if (core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER) {
      String dir = String(file_slots[core.osd[i].slot_id].dir);
      if (dir == "") { dir = "/"; }
      if (dir.charAt(0) != '/') { dir = '/' + dir; }
      dir.toCharArray(file_slots[core.osd[i].slot_id].dir, sizeof(file_slots[core.osd[i].slot_id].dir));
      String sfilename = String( file_slots[core.osd[i].slot_id].filename);
      String sfullname = dir + "/" + sfilename;
      if (sfilename.length() > 0 && sd1.exists(sfullname)) {
        // todo
        //file_slots[core.osd[i].slot_id].is_mounted = file_slots[core.osd[i].slot_id].file = sd1.open(sfullname, O_READ);
      }
      core_send(i);
    }
  }

  zxosd.hidePopup();
  if (is_osd) {
    zxosd.showMenu();
  } else {
    zxosd.hideMenu();
  }

  osd_handle(true);
}

// send a chunk of rom data to the fpga, starting at the given rom address
void rom_send(uint32_t addr, const uint8_t* buf, size_t len) {
  for (size_t j=0; j<len; j++) {
    uint32_t a = addr + j;
    // send rombank address every 256 bytes (and on the first byte of an unaligned chunk)
    if (j == 0 || a % 256 == 0) {
      spi_send(CMD_ROMBANK, 0, (uint8_t)((a & 0x0000FF00) >> 8));
      spi_send(CMD_ROMBANK, 1, (uint8_t)((a & 0x00FF0000) >> 16));
      spi_send(CMD_ROMBANK, 2, (uint8_t)((a & 0xFF000000) >> 24));
    }
    // send lower 256 bytes
    spi_send(CMD_ROMDATA, (uint8_t)(a & 0x000000FF), buf[j]);
  }
}

void rom_progress(uint32_t rom_idx, uint32_t sent) {
  zxosd.setPos(4,5+rom_idx);
  zxosd.print(rom_idx+1); zxosd.print(": ");
  char b[40];
  sprintf(b, "%05lu", (unsigned long) sent); zxosd.print(b); zxosd.print(" ");
  zxosd.update();
}

void read_roms(const char* filename) {

  sd1.chvol();
  if (!file1.open(filename, FILE_READ)) {
    halt("Unable to open bitstream file to read");
  }

  uint32_t bitstream_length = file_read32(FILE_POS_BITSTREAM_LEN);
  uint32_t roms_len = file_read32(FILE_POS_ROM_LEN);
  d_print("ROMS len "); d_println(roms_len);
  if (roms_len > 0) {
    spi_send(CMD_ROMLOADER, 0, 1);
  }
  uint32_t offset = 0;
  uint32_t rom_idx = 0;
  while (roms_len > 0) {
    uint32_t rom_pos = FILE_POS_BITSTREAM_START + bitstream_length + offset;
    uint8_t rom_hdr[KG_ROM_BLOCK_HEADER_SIZE] = {0};
    file_seek(rom_pos);
    file_read_bytes((char*) rom_hdr, sizeof(rom_hdr));
    kg_rom_block_t rom;
    bool rom_valid = kg_rom_block_parse(rom_hdr, roms_len, &rom);
    uint32_t rom_len = rom.len;
    bool rom_is_external = rom.external;
    uint32_t rom_addr = rom.addr;
    d_print("ROM #"); d_print(rom_idx); d_print(": addr="); d_print(rom_addr); d_print(", len="); d_println(rom_len);

    // sanity check to not run away on a broken core file
    if (!rom_valid) {
      d_println("ROM block is out of bounds, skipping the rest");
      break;
    }

    uint8_t buf[256];
    if (rom_is_external) {
      // external rom block contains a rom filename (rom_len bytes) instead of a rom data
      char rom_filename[256];
      size_t name_len = (rom_len < sizeof(rom_filename)) ? rom_len : sizeof(rom_filename) - 1;
      file_seek(rom_pos + KG_ROM_BLOCK_HEADER_SIZE);
      name_len = file_read_bytes(rom_filename, name_len);
      rom_filename[name_len] = '\0';
      d_print("External ROM "); d_println(rom_filename);

      if (sd1.exists(rom_filename)) {
        file2 = sd1.open(rom_filename, FILE_READ);
        uint32_t sent = 0;
        int c;
        while ((c = file2.read(buf, sizeof(buf))) > 0) {
          rom_send(rom_addr + sent, buf, c);
          sent += c;
          rom_progress(rom_idx, sent);
        }
        file2.close();
      } else {
        zxosd.setPos(4,5+rom_idx);
        zxosd.print(rom_idx+1); zxosd.print(": ");
        zxosd.print(" NO FILE");
        zxosd.update();
      }
    } else {
      file_seek(rom_pos + KG_ROM_BLOCK_HEADER_SIZE);
      uint32_t sent = 0;
      while (sent < rom_len) {
        size_t chunk = (rom_len - sent < sizeof(buf)) ? rom_len - sent : sizeof(buf);
        size_t c = file_read_bytes((char*) buf, chunk);
        if (c == 0) break;
        rom_send(rom_addr + sent, buf, c);
        sent += c;
        rom_progress(rom_idx, sent);
      }
    }
    offset = offset + rom_len + KG_ROM_BLOCK_HEADER_SIZE;
    roms_len = roms_len - rom_len - KG_ROM_BLOCK_HEADER_SIZE;
    rom_idx++;
    if (roms_len > 0) {
      // next rom
      zxosd.setPos(0, 5+rom_idx);
      zxosd.print("ROM ");
      zxosd.update();
    }
  }
  //delay(100);
  spi_send(CMD_ROMLOADER, 0, 0);

  file1.close();
}
