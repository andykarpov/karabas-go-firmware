#include "main.h"
#include "diag/hang_diag.h"
#include "ui/osd_font.h"
#include "input/ps2kbd.h"
#include "input/usb_hid_keys.h"
#include "kg_format.h"
#include "crc32.h"
#include "file_name.h"
#include "periph/printer_link.h"
#include "apps/app_file_loader.h"

// Loading a core file: bitstream, header (OSD items, eeprom, switches), ROMs.

core_item_t core;
core_file_slot_t file_slots[MAX_FILE_SLOTS];
static bool is_flashboot = false;

bool core_osd_can_phys(uint8_t pos) {
  return core.osd[pos].type == CORE_OSD_TYPE_FILEMOUNTER && (core.osd[pos].flags & CORE_OSD_FLAG_PHYS) &&
         board_has_floppy();
}

bool core_osd_phys(uint8_t pos, const char *filename) {
  return core_osd_can_phys(pos) && kg_phys_drive(filename) >= 0;
}

bool core_osd_is_file(uint8_t pos) {
  return pos < core.osd_len && (core.osd[pos].type == CORE_OSD_TYPE_FILEMOUNTER || core.osd[pos].type == CORE_OSD_TYPE_FILELOADER);
}

bool core_osd_selectable(uint8_t pos) {
  return pos < core.osd_len && (core.osd[pos].options_len > 0 || core_osd_is_file(pos) ||
                                core.osd[pos].type == CORE_OSD_TYPE_PAGE);
}

// the condition of an item met (no condition: met)
static bool cond_met(uint8_t pos) {
  const core_osd_t *item = &core.osd[pos];
  if (item->cond_item == 0) return true;
  bool eq = core.osd[item->cond_item - 1].val == (item->cond & CORE_OSD_COND_VALUE);
  return eq != ((item->cond & CORE_OSD_COND_INVERT) != 0);
}

bool core_osd_shown(uint8_t pos) {
  if (pos >= core.osd_len || core.osd[pos].type == CORE_OSD_TYPE_HIDDEN) return false;
  return (core.osd[pos].cond & CORE_OSD_COND_DISABLE) || cond_met(pos);
}

bool core_osd_enabled(uint8_t pos) {
  return core_osd_shown(pos) && cond_met(pos);
}

int core_osd_page_entry(uint8_t page) {
  for (uint8_t i = 0; i < core.osd_len; i++) {
    if (core.osd[i].type == CORE_OSD_TYPE_PAGE && core.osd[i].open_page == page) return i;
  }
  return -1;
}

void core_osd_change(uint8_t pos, int d) {
  core_osd_t *item = &core.osd[pos];
  if (item->options_len == 0) return;
  if (d > 0) item->val = item->val + 1 < item->options_len ? item->val + 1 : 0;
  else item->val = item->val > 0 ? item->val - 1 : item->options_len - 1;
  core_send(pos);
  if (item->type == CORE_OSD_TYPE_SWITCH) core.osd_need_save = true;
}

void core_send(uint8_t pos)
{
  if (core.osd[pos].type == CORE_OSD_TYPE_FILEMOUNTER) {
    img_mount(core.osd[pos].slot_id);
  } else if (core.osd[pos].type == CORE_OSD_TYPE_FILELOADER) {
    // do nothing
  } else {
    spi_send(CMD_SWITCHES, pos, core.osd[pos].val);
    d_printf("Switch: %s %d", core.osd[pos].name, core.osd[pos].val);
  }
  d_println();
}

#define SEND_PROGRESS_STEP 32768 // an OSD redraw costs: a popup every 8 KB made a 256 KB file 18% slower

uint32_t core_send_file(FIL *f, uint32_t len, uint8_t data_cmd, uint8_t bank_cmd, uint32_t addr,
    core_send_progress_t progress, void *ctx, uint32_t *crc) {
  uint8_t *buf = bitstream_buf(); // free while no bitstream is sent
  uint32_t sent = 0, shown = 0;
  UINT n;
  while (sent < len && (n = sd_read(f, buf, len - sent < BITSTREAM_BUF_SIZE ? len - sent : BITSTREAM_BUF_SIZE)) > 0) {
    if (crc) *crc = kg_crc32_update(*crc, buf, n);
    link_write_data(data_cmd, bank_cmd, addr + sent, buf, n);
    sent += n;
    if (progress && sent - shown >= SEND_PROGRESS_STEP) {
      progress(sent, ctx);
      shown = sent;
    }
  }
  if (progress && sent != shown) progress(sent, ctx); // the end
  return sent;
}

void core_loading_popup(uint32_t sent, void *ctx) {
  osd_loading_popup(sent, *(const uint32_t *) ctx);
  osd_update();
}

void core_ioctl_send(uint8_t slot, bool popup)
{
  if (slot >= MAX_FILE_SLOTS) return;
  if (popup) {
    osd_loading_popup(0, 100);
    osd_update();
  }

  // send ioctl slot id, file data for file loader type
  spi_bus_lock(); // keep the whole ioctl transfer together on the fpga link
  spi_send(CMD_IOCTL_STATE, 0, 1); // start
  spi_send(CMD_IOCTL_SLOT, 0, slot);

  uint64_t fsize = 0;
  spi_send64(CMD_IOCTL_SIZE, fsize);
  char fname[KG_PATH_MAX];
  kg_path_join(fname, sizeof(fname), file_slots[slot].dir, file_slots[slot].filename);
  kg_path_collapse(fname);
  FIL *file = sd_tmp_open(fname, FA_READ);
  bool open = file != NULL;
  if (!open) {
    d_printf("Ioctl slot %d: unable to open %s", slot, fname); d_println();
  }
  fsize = open ? f_size(file) : 0;
  spi_send64(CMD_IOCTL_SIZE, fsize);

  const char *dot = strrchr(fname, '.');
  const char *ext = dot ? dot + 1 : fname;
  for(uint8_t i = 0; ext[i] && i < 255; i++) {
    spi_send(CMD_IOCTL_EXT, i, ext[i]);
  }

  uint32_t send_timer = ms_now();
  uint32_t total = (uint32_t) fsize;
  uint32_t i = open ? core_send_file(file, total, CMD_IOCTL_DATA, CMD_IOCTL_BANK, 0, popup ? core_loading_popup : NULL, &total, NULL) : 0;
  sd_tmp_close();
  spi_send(CMD_IOCTL_STATE, 0, 0); // finish
  d_printf("Ioctl slot %d: %s, %lu bytes in %lu ms (%s)", slot, fname, (unsigned long) i,
    (unsigned long) ms_since(send_timer), link_burst_available() ? "burst" : "classic");
  d_println();

  if (popup) {
    osd_loading_popup(fsize, fsize);
    osd_update();
  }
  spi_bus_unlock();
}

void core_send_all() 
{
  for (uint8_t i=0; i<core.osd_len; i++) {
    if (core_osd_is_file(i)) {
      // images are mounted by read_core(), after the core settings
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
    ft_reset();
    // reset ESP and MIDI on soft reset (mini revG)
    board_reset_peripherals();
    ms_delay(100);
  }

  spi_send(CMD_SWITCHES, pos, 1);
  ms_delay(100);
  spi_send(CMD_SWITCHES, pos, 0);
}

// The EEPROM of banks 4..254 is kept in the core file (FILE_POS_EEPROM_DATA, all 256 bytes in
// the first sector of the file). Cores write the rtc / cmos registers often, mostly with the
// values they already have, so the SD card is spared:
// - prev_val is the byte in the file: a write of the same value, or a change undone, leaves
//   nothing to save (eeprom_dirty counts the bytes that differ from the file);
// - the save waits until the core stops writing (EEPROM_SAVE_QUIET_MS), or for a core that never
//   stops, until a change is EEPROM_SAVE_MAX_MS old; never sooner than EEPROM_SAVE_GAP_MS after
//   the save before, so a chatty core costs at most a write every 30 s;
// - one write of the 256 bytes (one sector), and core_eeprom_flush() before another core, a
//   reboot, BOOTSEL and USB MSC, so nothing waits in RAM then.
#define EEPROM_SAVE_QUIET_MS 2000
#define EEPROM_SAVE_MAX_MS 60000
#define EEPROM_SAVE_GAP_MS 30000
static uint16_t eeprom_dirty = 0;
static uint32_t eeprom_first_change, eeprom_last_change, eeprom_last_save;
static bool eeprom_saved = false; // eeprom_last_save is set

uint8_t core_eeprom_get(uint8_t pos) {
  return core.eeprom[pos].val;
}

void core_eeprom_set(uint8_t pos, uint8_t val) {
  core_eeprom_t *e = &core.eeprom[pos];
  if (e->val == val) return;
  if (e->val == e->prev_val) {
    if (eeprom_dirty == 0) eeprom_first_change = ms_now();
    eeprom_dirty++;
  } else if (val == e->prev_val) {
    eeprom_dirty--;
  }
  e->val = val;
  eeprom_last_change = ms_now();
}

bool core_eeprom_flush(void) {
  if (eeprom_dirty == 0) return true;
  eeprom_last_save = ms_now(); // a failed write is tried again after the gap too
  eeprom_saved = true;
  if (sd_is_open(&file1)) {
    d_println("Core EEPROM: the core file is busy, saved later");
    return false;
  }
  if (!sd_open(&file1, core.filename, FA_READ | FA_WRITE)) {
    d_printf("Core EEPROM: unable to open %s to write", core.filename); d_println();
    return false;
  }
  uint8_t buf[MAX_EEPROM_ITEMS];
  for (uint16_t i = 0; i < MAX_EEPROM_ITEMS; i++) buf[i] = core.eeprom[i].val;
  file_seek(FILE_POS_EEPROM_DATA);
  bool ok = sd_write(&file1, buf, sizeof(buf)) == sizeof(buf);
  sd_close(&file1);
  if (!ok) {
    d_println("Core EEPROM: write failed");
    return false;
  }
  d_printf("Core EEPROM: saved, %u bytes changed", eeprom_dirty); d_println();
  for (uint16_t i = 0; i < MAX_EEPROM_ITEMS; i++) core.eeprom[i].prev_val = core.eeprom[i].val;
  eeprom_dirty = 0;
  return true;
}

void core_eeprom_tick(void) {
  if (eeprom_dirty == 0) return;
  if (ms_since(eeprom_last_change) < EEPROM_SAVE_QUIET_MS && ms_since(eeprom_first_change) < EEPROM_SAVE_MAX_MS) return;
  if (eeprom_saved && ms_since(eeprom_last_save) < EEPROM_SAVE_GAP_MS) return;
  core_eeprom_flush();
}

void flashboot (uint8_t data) {
  if (is_flashboot) return; // skip already in flashboot mode
  d_printf("Flashboot core id: %02x", data); d_println();
  d_flush(); ms_delay(100);
  // todo: add flashboot_id to the core structure, search for desired id and boot it
  // now it's implemented as hack: reload the same core (required for zx next hard reset)
  // d_printf("Core id %s", core.id); d_println();
  // d_printf("Core filename %s", core.filename); d_println();
  if (strcmp(core.id, "zxnext") == 0) { // allow flashboot only for zxnext core
    is_flashboot = true;
    do_reload();
  }
}

// hang_step: the steps of a core load (0x8x, 0x9x in fpga_send, 0xax after it)
// the build ids of the core (its .kg header) and of this firmware, 8 ASCII characters each (shorter: 0 padded),
// for cores that show them (BaseConf: the version strings of its RTC registers F0..FF)
static void send_build_ids(void) {
  static const char fw_build[] = STRINGIZE_VALUE_OF(BUILD_VER);
  bool end_core = false, end_fw = false;
  for (uint8_t i=0; i<8; i++) {
    if (!core.build[i]) end_core = true;
    if (!fw_build[i]) end_fw = true;
    spi_send(CMD_HW_SETUP, 0x10 + i, end_core ? 0 : (uint8_t) core.build[i]);
    spi_send(CMD_HW_SETUP, 0x18 + i, end_fw ? 0 : (uint8_t) fw_build[i]);
  }
}

// the boot core fills the SRAM with zeros after its start (0.2 s, sram_clear of karabas-go-core-menu): a core loaded
// again, by the reload action or CMD_FLASHBOOT, goes through it, so it starts with clean memory as from the menu
// (BaseConf keeps the found partitions in RAM and did not see an HDD image mounted later)
#define SRAM_CLEAR_MS 250

static void configure(const char* filename, bool clear_sram) {
  hang_step(0x81);
  printer_link_reset();
  // the whole core load (bitstream, setup, roms) is one sequence on the fpga link
  hang_step(0x82);
  spi_bus_lock();
  hang_step(0x83);
  matrix_on_configure_start();
  bk_flush(); // the save of the game of the core before (it is still running)
  core_eeprom_flush(); // and its eeprom (bank 4..254), before core is read again
  hang_step(0x84);
  link_set_rx_enabled(false);
  link_burst_reset(); // the new core may not support bursts
  hang_step(0x85);
  ft_vga(false); // FT off
  ft_spi(false);
  hang_step(0x86);
  kb_reset(); // reset to ps/2 defaults

  hang_step(0x87);
  if (clear_sram && sd_exists(FILENAME_BOOT)) {
    d_println("SRAM clear by the boot core");
    fpga_send(FILENAME_BOOT);
    ms_delay(SRAM_CLEAR_MS);
  }
  fpga_send(filename);
  hang_step(0xa0);
  // wait for the core to answer before anything else goes to it (frames sent while its link is
  // still in reset are lost: a Kickstart sent then never arrived); burst transfers for the roms /
  // files below, if the core supports them
  link_caps_query();
  hang_step(0xa1);
  spi_send(CMD_INIT_START, 0, 0);
  spi_send(CMD_HW_SETUP, 0, HW_ID); // hw id
  spi_send(CMD_HW_SETUP, 1, hw_setup.dvi_only); // dvi only flag
  // trigger font loader reset
  hang_step(0xa2);
  osd_font_reset();
  // send font data
  for (int i=0; i<OSD_FONT_SIZE; i++) {
    osd_font_send(osd_font[i]);
  }
  hang_step(0xa3);
  read_core(filename);
  send_build_ids();
  hang_step(0xa4);
  if (!is_osd) {
    osd_clear();
    osd_logo(0,0, HW_ID);
    osd_set_color(OSD_COLOR_WHITE, OSD_COLOR_BLACK);
    osd_set_pos(0,5);
    osd_print("ROM ");
    osd_update();
    osd_show_popup(0);
  }
  hang_step(0xa5);
  read_roms(filename);
  hang_step(0xa6);
  if (!is_osd) {
    osd_hide_popup();
    osd_handle(true); // reinit osd
  }
  for (uint8_t i=0; i<7; i++) { // cleanup kbd: modifier + 6 keycodes
    spi_queue(CMD_USB_KBD, i, 0);
  }
  hang_step(0xa7);
  spi_send(CMD_INIT_DONE, 0, 0);
  is_flashboot = false;
  link_set_rx_enabled(true);

  // reinit matrix
  hang_step(0xa8);
  matrix_on_configure_done(core.type);
  hang_step(0xa9);
  spi_bus_unlock();
}

void do_configure(const char* filename) {
  configure(filename, false);
}

void do_reload(void) {
  char filename[sizeof(core.filename)];
  strcpy(filename, core.filename); // read_core() writes core.filename
  configure(filename, true);
}

// the on-board button actions; FF (older core files): press goes to the core, no hold
static void read_buttons() {
  static const char *const names[BTN_ACTION_COUNT] = {"core", "none", "osd", "menu", "reload", "bootsel", "msc", "msc2"};
  file_seek(FILE_POS_BUTTONS);
  for (uint8_t i=0; i<CORE_BUTTONS; i++) {
    uint8_t press = file_read();
    uint8_t hold = file_read();
    uint8_t hold_time = file_read();
    core.btn[i].press = press < BTN_ACTION_COUNT ? press : BTN_ACTION_CORE;
    core.btn[i].hold = (hold < BTN_ACTION_COUNT && hold_time > 0 && hold_time != 0xFF) ? hold : BTN_ACTION_NONE;
    core.btn[i].hold_ms = core.btn[i].hold == BTN_ACTION_NONE ? 0 : hold_time * 100;
    d_printf("Button %u: %s", i+1, names[core.btn[i].press]);
    if (core.btn[i].hold != BTN_ACTION_NONE) d_printf(", hold %u ms: %s", core.btn[i].hold_ms, names[core.btn[i].hold]);
    d_println();
  }
}

// the LED matrix settings; zeros (older core files): the core draws
static void read_matrix() {
  static const char *const modes[KG_MATRIX_MODE_COUNT] = {"core", "scroll", "effect", "image", "blank"};
  static uint8_t block[KG_MATRIX_BLOCK_SIZE];
  static kg_matrix_cfg_t cfg;
  file_seek(FILE_POS_MATRIX);
  file_read_bytes((char *) block, sizeof(block));
  kg_matrix_parse(block, &cfg);
  matrix_set_config(&cfg, core.name, core.build);
  d_printf("Matrix: %s", modes[cfg.mode]);
  if (cfg.mode == KG_MATRIX_MODE_SCROLL) d_printf(" \"%s\"", cfg.text);
  if (cfg.mode == KG_MATRIX_MODE_EFFECT) d_printf(" %u", cfg.effect);
  if (cfg.mode == KG_MATRIX_MODE_IMAGE) d_printf(" %u frame(s)", cfg.frame_count);
  if (cfg.repeat) d_printf(", %u times, then %s", cfg.repeat, modes[cfg.then]);
  d_println();
}

void read_core(const char* filename) {

  if (!sd_open(&file1, filename, FA_READ)) {
    halt("Unable to open bitstream file to read");
  }

  {
    FILINFO fi; // the name as it is on the card
    snprintf(core.filename, sizeof(core.filename), "%s", f_stat(filename, &fi) == FR_OK ? fi.fname : filename);
    core.filename[32] = '\0';
  }
  file_read_str(&file1, FILE_POS_CORE_NAME, core.name, 32);
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
  file_read_str(&file1, FILE_POS_CORE_ID, core.id, 32);
  file_read_str(&file1, FILE_POS_CORE_BUILD, core.build, 8);
  file_seek(FILE_POS_CORE_EEPROM_BANK); core.eeprom_bank = file_read();
  file_seek(FILE_POS_RTC_TYPE); core.rtc_type = file_read();
  file_read_str(&file1, FILE_POS_FILELOADER_DIR, core.dir, 32);
  file_seek(FILE_POS_FILELOADER_FILE); core.last_file_id = file_read16(FILE_POS_FILELOADER_FILE);
  file_read_str(&file1, FILE_POS_FILELOADER_EXTENSIONS, core.file_extensions, 32);
  file_seek(FILE_POS_SPI_FREQ); core.spi_freq = file_read();
  file_read_str(&file1, FILE_POS_SAVE_DIR, core.save_dir, 32); // older core files: FF, no save dir
  link_set_freq(core.spi_freq);
  read_buttons();
  read_matrix();
  uint32_t roms_len = file_read32(FILE_POS_ROM_LEN);
  uint32_t offset = FILE_POS_BITSTREAM_START + core.bitstream_length + roms_len;
  //d_print("OSD section: "); d_println(offset);
  file_seek(offset); core.osd_len = file_read();
  // a broken core file (cut short, a wrong roms_len) reads FF here: no more items than fit
  if (core.osd_len > MAX_OSD_ITEMS) {
    d_printf("OSD section: %u items, only %d fit", core.osd_len, MAX_OSD_ITEMS); d_println();
    core.osd_len = MAX_OSD_ITEMS;
  }
  //d_print("OSD len: "); d_println(core.osd_len);

  if (core.spi_freq > 0 && core.spi_freq < 255) { d_printf("Core SPI Frequency: %u MHz", core.spi_freq); d_println(); } else { d_println("Core SPI Frequency: default"); }
  
  app_file_loader_core_loaded();
  img_close_all(); // the images of the previous core
  bk_forget();
  for (uint8_t i=0; i<MAX_FILE_SLOTS; i++) { file_slots[i].is_mounted = false; }

  for (uint8_t i=0; i<core.osd_len; i++) {
    if (f_tell(&file1) >= f_size(&file1)) {
      d_printf("OSD section: the file ends at item %u", i); d_println();
      core.osd_len = i;
      break;
    }
    core.osd[i].type = file_read();
    core.osd[i].flags = file_read();
    file_read_bytes(core.osd[i].name, 16); core.osd[i].name[16] = '\0';
    core.osd[i].def = file_read();
    core.osd[i].val = core.osd[i].def;
    core.osd[i].prev_val = core.osd[i].def;

    // filemounter osd type:
    // loading initial dir, filename, extensions and trying to mount file, if any
    if (core_osd_is_file(i)) {
      core.osd[i].options_len = 0;
      uint8_t slot_byte = file_read(); // bit 7: autoload
      // not bitClear(): it clears the bit in slot_byte too, and the autoload bit read 0
      core.osd[i].slot_id = slot_byte & 0x7F;
      if (core.osd[i].slot_id >= MAX_FILE_SLOTS) {
        d_printf("OSD item %u: file slot %u, only %d exist", i, core.osd[i].slot_id, MAX_FILE_SLOTS); d_println();
        core.osd_len = i; // the items after it can't be trusted either
        break;
      }
      file_slots[core.osd[i].slot_id].is_autoload = (slot_byte & 0x80) != 0;
      file_slots[core.osd[i].slot_id].is_mounted = false;
      file_read_bytes(file_slots[core.osd[i].slot_id].ext, 256); file_slots[core.osd[i].slot_id].ext[255] = '\0';
      file_slots[core.osd[i].slot_id].offset_dir = f_tell(&file1);
      file_read_bytes(file_slots[core.osd[i].slot_id].dir, 256); file_slots[core.osd[i].slot_id].dir[255] = '\0';
      char *dir = file_slots[core.osd[i].slot_id].dir;
      kg_trim(dir); // make_core pads with spaces
      kg_path_root(dir, sizeof(file_slots[0].dir));
      file_slots[core.osd[i].slot_id].offset_filename = f_tell(&file1);
      file_read_bytes(file_slots[core.osd[i].slot_id].filename, 256); file_slots[core.osd[i].slot_id].filename[255] = '\0';
      char *sfilename = file_slots[core.osd[i].slot_id].filename;
      kg_trim(sfilename);
      char sfullname[KG_PATH_MAX];
      kg_path_join(sfullname, sizeof(sfullname), dir, sfilename);
      if (sfilename[0] && (sd_exists(sfullname) || core_osd_phys(i, sfilename))) {
        file_slots[core.osd[i].slot_id].is_mounted = true;
        // autoload: FL items below, after the switches (F items are always mounted)
      }
    }
      // otherwise - reading options structure
      else {
      uint8_t n = file_read();
      core.osd[i].options_len = n > MAX_OSD_ITEM_OPTIONS ? MAX_OSD_ITEM_OPTIONS : n;
      for (uint8_t j=0; j<core.osd[i].options_len; j++) {
        file_read_bytes(core.osd[i].options[j].name, 16); core.osd[i].options[j].name[16] = '\0';
      }
      // the names that do not fit are skipped, so the rest of the section stays in step
      if (n > core.osd[i].options_len) file_seek(f_tell(&file1) + (n - core.osd[i].options_len) * 16);
      // an option name is shown by its index (a hidden item without options keeps its value)
      if (core.osd[i].options_len > 0 && core.osd[i].def >= core.osd[i].options_len) {
        core.osd[i].def = 0;
        core.osd[i].val = 0;
        core.osd[i].prev_val = 0;
      }
    }
    file_read_bytes(core.osd[i].hotkey, 16); core.osd[i].hotkey[16] = '\0';
    core.osd[i].keys[0] = file_read();
    core.osd[i].keys[1] = file_read();
    // the page and the condition (0 0 0 in core files before the tree menus: the main page,
    // always shown)
    core.osd[i].page = file_read();
    core.osd[i].cond_item = file_read();
    core.osd[i].cond = file_read();
    core.osd[i].open_page = 0;
    if (core.osd[i].type == CORE_OSD_TYPE_PAGE) {
      core.osd[i].open_page = core.osd[i].def; // a page entry has no value of its own
      core.osd[i].def = core.osd[i].val = core.osd[i].prev_val = 0;
    }
    if (core.osd[i].cond_item > core.osd_len) core.osd[i].cond_item = 0;
  }

  // read eeprom data from file (in case rombank = 4 and up)
  // 255 means no eeprom allowed by core
  eeprom_dirty = 0;
  if (core.eeprom_bank >= MAX_EEPROM_BANKS && core.eeprom_bank != NO_EEPROM_BANK) {
    for (uint16_t i=0; i<MAX_EEPROM_ITEMS; i++) {
      file_seek(FILE_POS_EEPROM_DATA + i);
      core.eeprom[i].val = file_read();
      core.eeprom[i].prev_val = core.eeprom[i].val;
    }
  }
  rtc_set_eeprom_bank(core.eeprom_bank);
  rtc_set_rtc_type(core.rtc_type);
  rtc_send_all();

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
  rtc_send_all();

  has_ft = false;

  // hw ft reset for other cores types
  if (core.type != CORE_TYPE_BOOT) {
    ft_reset();
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
        ft_spi(true);
        has_ft = ft_init(hw_setup.ft_video_mode);
        if (ft_init_failures()[0]) {
          d_printf("FT81x failed attempts: %s", ft_init_failures()); d_println();
        }
        if (has_ft) {
          d_printf("Found FT81x IC (system clock %lu kHz, attempt %u), switching to FT OSD",
            (unsigned long) (ft_init_clock_hz() / 1000), ft_init_attempts()); d_println();
          // with no picture: 0 frames is the chip's side, running frames the video path of the core
          // (on the Mini boards its hdmi_pll locks to the FT812 pixel clock)
          if (hw_setup.debug_enabled) {
            d_printf("FT81x scanout: %lu frames in 100 ms", (unsigned long) ft_frames_in(100)); d_println();
          }
          ft_vga(true);
        } else {
          d_printf("FT81x IC did not found (%u attempts, last system clock %lu kHz)", ft_init_attempts(),
            (unsigned long) (ft_init_clock_hz() / 1000)); d_println();
          ft_vga(false);
          ft_spi(false);
        }
      }
    }
  }

  // dump parsed OSD items
  for(uint8_t i=0; i<core.osd_len; i++) {
    d_printf("OSD %d: type: %d name: %s def: %d len: %d keys: [%d %d]", i, core.osd[i].type, core.osd[i].name, core.osd[i].def, core.osd[i].options_len, core.osd[i].keys[0], core.osd[i].keys[1]); 
    d_println();
    for (uint8_t j=0; j<core.osd[i].options_len; j++) {
      d_print(core.osd[i].options[j].name); d_print(", "); 
    } 
    d_println();
    d_print(core.osd[i].hotkey); d_println();
  }

  sd_close(&file1);

  // mount slots
  for(uint8_t i=0; i<core.osd_len; i++) {
    if (core.osd[i].type == CORE_OSD_TYPE_FILEMOUNTER) {
      char *dir = file_slots[core.osd[i].slot_id].dir;
      kg_path_root(dir, sizeof(file_slots[0].dir));
      const char *sfilename = file_slots[core.osd[i].slot_id].filename;
      char sfullname[KG_PATH_MAX];
      kg_path_join(sfullname, sizeof(sfullname), dir, sfilename);
      file_slots[core.osd[i].slot_id].is_mounted = sfilename[0] &&
        (sd_exists(sfullname) || core_osd_phys(i, sfilename));
      core_send(i); // opens the image and sends its size (or the physical drive)
    }
    // FL items with autoload: the file goes to the core before it starts (a Kickstart rom)
    else if (core.osd[i].type == CORE_OSD_TYPE_FILELOADER && file_slots[core.osd[i].slot_id].is_autoload &&
             file_slots[core.osd[i].slot_id].is_mounted) {
      core_ioctl_send(core.osd[i].slot_id, false);
    }
    else if (core.osd[i].type == CORE_OSD_TYPE_FILELOADER && file_slots[core.osd[i].slot_id].is_autoload) {
      d_printf("Ioctl slot %d: no file to autoload (%s/%s)", core.osd[i].slot_id,
        file_slots[core.osd[i].slot_id].dir, file_slots[core.osd[i].slot_id].filename); d_println();
    }
  }

  osd_hide_popup();
  if (is_osd) {
    osd_show_menu();
  } else {
    osd_hide_menu();
  }

  osd_handle(true);
}

// the bytes sent of the ROM *ctx (uint32_t, from 0) on its row of the loading screen
static void rom_progress(uint32_t sent, void *ctx) {
  uint32_t rom_idx = *(const uint32_t *) ctx;
  osd_set_pos(4,5+rom_idx);
  osd_printf("%lu: ", (unsigned long) rom_idx + 1);
  char b[40];
  sprintf(b, "%05lu", (unsigned long) sent); osd_print(b); osd_print(" ");
  osd_update();
}


void read_roms(const char* filename) {

  if (!sd_open(&file1, filename, FA_READ)) {
    halt("Unable to open bitstream file to read");
  }

  uint32_t bitstream_length = file_read32(FILE_POS_BITSTREAM_LEN);
  uint32_t roms_len = file_read32(FILE_POS_ROM_LEN);
  d_printf("ROMS len %lu", (unsigned long) roms_len); d_println();
  if (roms_len > 0) {
    spi_send(CMD_ROMLOADER, 0, 1);
  }
  uint32_t rom_timer = ms_now();
  rom_timer = ms_now();
  uint32_t rom_bytes = 0;
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
    d_printf("ROM #%lu: addr=%lu, len=%lu", (unsigned long) rom_idx, (unsigned long) rom_addr, (unsigned long) rom_len); d_println();

    // sanity check to not run away on a broken core file
    if (!rom_valid) {
      d_println("ROM block is out of bounds, skipping the rest");
      break;
    }

    if (rom_is_external) {
      // external rom block contains a rom filename (rom_len bytes) instead of a rom data
      char rom_filename[256];
      size_t name_len = (rom_len < sizeof(rom_filename)) ? rom_len : sizeof(rom_filename) - 1;
      file_seek(rom_pos + KG_ROM_BLOCK_HEADER_SIZE);
      name_len = file_read_bytes(rom_filename, name_len);
      rom_filename[name_len] = '\0';
      d_print("External ROM "); d_println(rom_filename);

      if (sd_open(&file2, rom_filename, FA_READ)) {
        rom_bytes += core_send_file(&file2, UINT32_MAX, CMD_ROMDATA, CMD_ROMBANK, rom_addr, rom_progress, &rom_idx, NULL);
        sd_close(&file2);
      } else {
        osd_set_pos(4,5+rom_idx);
        osd_printf("%lu:  NO FILE", (unsigned long) rom_idx + 1);
        osd_update();
      }
    } else {
      file_seek(rom_pos + KG_ROM_BLOCK_HEADER_SIZE);
      rom_bytes += core_send_file(&file1, rom_len, CMD_ROMDATA, CMD_ROMBANK, rom_addr, rom_progress, &rom_idx, NULL);
    }
    offset = offset + rom_len + KG_ROM_BLOCK_HEADER_SIZE;
    roms_len = roms_len - rom_len - KG_ROM_BLOCK_HEADER_SIZE;
    rom_idx++;
    if (roms_len > 0) {
      // next rom
      osd_set_pos(0, 5+rom_idx);
      osd_print("ROM ");
      osd_update();
    }
  }
  //delay(100);
  spi_send(CMD_ROMLOADER, 0, 0);

  if (rom_bytes > 0) {
    d_printf("ROMs: %lu bytes in %lu ms (%s)", (unsigned long) rom_bytes, (unsigned long) ms_since(rom_timer),
      link_burst_available() ? "burst" : "classic");
    d_println();
  }

  sd_close(&file1);
}
