#include "main.h"
#include "fpga/core_file.h"
#include "kg_format.h"
#include "file_name.h"

bool core_file_is_core(const char *name) {
  return kg_name_has_ext(name, CORE_EXT) || kg_name_has_ext(name, KG2_EXT);
}

static bool read_at(FIL *f, uint32_t pos, void *buf, size_t len) {
  return f_lseek(f, pos) == FR_OK && sd_read(f, buf, len) == len;
}

bool core_file_open(FIL *f, const char *name, core_file_t *cf) {
  memset(cf, 0, sizeof(*cf));
  uint32_t size = f_size(f);
  uint8_t head[KG2_HEADER_SIZE];
  if (!read_at(f, 0, head, sizeof(head))) return false;

  if (kg_name_has_ext(name, KG2_EXT)) {
    kg2_header_t h;
    if (!kg2_header_parse(head, sizeof(head), &h)) return false;
    size_t table_len = (size_t) h.count * h.entry_size;
    uint8_t table[KG2_MAX_SECTIONS * KG2_ENTRY_SIZE];
    if (table_len > sizeof(table) || !read_at(f, h.header_size, table, table_len)) return false;
    if (!kg2_layout_parse(&h, table, table_len, size, KG2_BOARD_BIT(HW_ID), &cf->kg2)) return false;
    cf->format = CORE_FORMAT_KGO2;
    int bs = cf->kg2.sec[KG2_SEC_BITSTREAM_LINK2].present ? KG2_SEC_BITSTREAM_LINK2 : KG2_SEC_BITSTREAM;
    cf->link = bs == KG2_SEC_BITSTREAM_LINK2 ? 2 : 1;
    cf->bitstream_ofs = cf->kg2.sec[bs].offset;
    cf->bitstream_len = cf->kg2.sec[bs].length;
    cf->roms_ofs = cf->kg2.sec[KG2_SEC_ROMS].offset;
    cf->roms_len = cf->kg2.sec[KG2_SEC_ROMS].length;
    return true;
  }

  // kgo1: the lengths in the header, the bitstream at FILE_POS_BITSTREAM_START, the roms after it
  uint8_t len[8];
  if (!read_at(f, FILE_POS_BITSTREAM_LEN, len, sizeof(len))) return false;
  cf->format = CORE_FORMAT_KGO1;
  cf->link = 1;
  cf->bitstream_ofs = FILE_POS_BITSTREAM_START;
  cf->bitstream_len = kg_be32(len);
  cf->roms_ofs = FILE_POS_BITSTREAM_START + cf->bitstream_len;
  cf->roms_len = kg_be32(len + 4);
  return true;
}

void core_file_str(FIL *f, const core_file_t *cf, uint16_t ref, char *out, size_t size) {
  out[0] = '\0';
  const kg2_section_t *s = &cf->kg2.sec[KG2_SEC_STRINGS];
  if (cf->format != CORE_FORMAT_KGO2 || size == 0 || ref >= s->length) return;
  size_t n = s->length - ref < size - 1 ? s->length - ref : size - 1;
  if (f_lseek(f, s->offset + ref) != FR_OK) return;
  n = sd_read(f, out, n);
  out[n] = '\0'; // a longer string is cut
}

bool core_file_sec_read(FIL *f, const core_file_t *cf, int type, uint32_t ofs, void *buf, size_t len) {
  if (cf->format != CORE_FORMAT_KGO2 || type <= 0 || type >= KG2_SEC_COUNT) return false;
  const kg2_section_t *s = &cf->kg2.sec[type];
  if (!s->present || ofs > s->length || len > s->length - ofs) return false;
  return read_at(f, s->offset + ofs, buf, len);
}

// a kgo1 header string (fixed length, padded with spaces or FF)
static void kg1_str(FIL *f, uint32_t pos, char *out, size_t len) {
  file_read_str(f, pos, out, len);
}

void core_file_info(FIL *f, const core_file_t *cf, core_list_item_t *out) {
  if (cf->format == CORE_FORMAT_KGO2) {
    uint8_t rec[KG2_CORE_SIZE];
    kg2_core_t c;
    memset(&c, 0, sizeof(c));
    if (core_file_sec_read(f, cf, KG2_SEC_CORE, 0, rec, sizeof(rec))) kg2_core_parse(rec, sizeof(rec), &c);
    core_file_str(f, cf, c.id, out->id, sizeof(out->id));
    core_file_str(f, cf, c.name, out->name, sizeof(out->name));
    core_file_str(f, cf, c.build, out->build, sizeof(out->build));
    out->visible = c.visible > 0;
    out->order = c.order;
    out->type = c.type;
    return;
  }
  kg1_str(f, FILE_POS_CORE_ID, out->id, 32);
  kg1_str(f, FILE_POS_CORE_NAME, out->name, 32);
  kg1_str(f, FILE_POS_CORE_BUILD, out->build, 8);
  uint8_t b[3] = { 0, 0, 0 };
  read_at(f, FILE_POS_CORE_VISIBLE, b, sizeof(b)); // visible, order, type
  out->visible = b[0] > 0;
  out->order = b[1];
  out->type = b[2];
}

const char *core_file_boot(void) {
  if (!sd_exists(FILENAME_BOOT_KG2)) return FILENAME_BOOT;
  FIL *f = sd_tmp_open(FILENAME_BOOT_KG2, FA_READ);
  if (f == NULL) return FILENAME_BOOT; // the shared FIL is taken: no .kg check now
  core_file_t cf;
  bool ok = core_file_open(f, FILENAME_BOOT_KG2, &cf);
  sd_tmp_close();
  return ok ? FILENAME_BOOT_KG2 : FILENAME_BOOT;
}
