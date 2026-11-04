#include "kg2_format.h"
#include <string.h>

uint16_t kg_le16(const uint8_t *buf) {
  return (uint16_t) (buf[0] | (buf[1] << 8));
}

uint32_t kg_le32(const uint8_t *buf) {
  return (uint32_t) buf[0] | ((uint32_t) buf[1] << 8) | ((uint32_t) buf[2] << 16) | ((uint32_t) buf[3] << 24);
}

bool kg2_header_parse(const uint8_t *buf, size_t len, kg2_header_t *h) {
  if (len < KG2_HEADER_SIZE || memcmp(buf, KG2_SIGNATURE, 4) != 0) return false;
  h->version = buf[4];
  h->header_size = kg_le16(buf + 6);
  h->count = kg_le16(buf + 8);
  h->entry_size = kg_le16(buf + 10);
  h->boards = kg_le16(buf + 12);
  return h->version >= 1 && h->header_size >= KG2_HEADER_SIZE && h->entry_size >= KG2_ENTRY_SIZE &&
         h->count > 0 && h->count <= KG2_MAX_SECTIONS;
}

bool kg2_layout_parse(const kg2_header_t *h, const uint8_t *table, size_t len, uint32_t file_size,
                      uint16_t board_bit, kg2_layout_t *out) {
  memset(out, 0, sizeof(*out));
  if ((size_t) h->count * h->entry_size > len) return false;
  for (uint16_t i = 0; i < h->count; i++) {
    const uint8_t *e = table + (size_t) i * h->entry_size;
    uint16_t type = kg_le16(e);
    uint16_t boards = kg_le16(e + 2);
    uint32_t ofs = kg_le32(e + 4);
    uint32_t length = kg_le32(e + 8);
    if (ofs > file_size || length > file_size - ofs) return false;
    // another board's section, or a type of a newer format: skipped
    if (!(boards & board_bit) || type == 0 || type >= KG2_SEC_COUNT) continue;
    kg2_section_t *s = &out->sec[type];
    if (s->present) return false; // two sections of a type for one board
    s->offset = ofs;
    s->length = length;
    s->present = true;
  }
  return out->sec[KG2_SEC_STRINGS].present && out->sec[KG2_SEC_CORE].present &&
         out->sec[KG2_SEC_BITSTREAM].present;
}

const char *kg2_str(const uint8_t *strings, size_t len, uint16_t ref) {
  if (ref >= len || memchr(strings + ref, '\0', len - ref) == NULL) return "";
  return (const char *) strings + ref;
}

bool kg2_core_parse(const uint8_t *sec, size_t len, kg2_core_t *out) {
  if (len < KG2_CORE_SIZE || kg_le16(sec) < KG2_CORE_SIZE) return false;
  out->id = kg_le16(sec + 2);
  out->name = kg_le16(sec + 4);
  out->build = kg_le16(sec + 6);
  out->type = sec[8];
  out->visible = sec[9];
  out->order = sec[10];
  out->rtc_mode = sec[11];
  out->eeprom_bank = sec[12];
  out->spi_freq = sec[13];
  out->drives = kg_le16(sec + 14);
  return true;
}

bool kg2_loader_parse(const uint8_t *sec, size_t len, kg2_loader_t *out) {
  if (len < KG2_LOADER_SIZE || kg_le16(sec) < KG2_LOADER_SIZE) return false;
  out->dir = kg_le16(sec + 2);
  out->exts = kg_le16(sec + 4);
  out->save_dir = kg_le16(sec + 6);
  out->flags = sec[8];
  out->files = sec[9];
  out->files_ofs = kg_le16(sec + 10);
  if (out->files && (size_t) out->files_ofs + 2u * out->files > len) out->files = 0;
  return true;
}

bool kg2_ref_at(const uint8_t *sec, size_t len, uint16_t ofs, uint8_t n, uint16_t *ref) {
  size_t at = (size_t) ofs + 2u * n;
  if (at + 2 > len) return false;
  *ref = kg_le16(sec + at);
  return true;
}

bool kg2_osd_head_parse(const uint8_t *head, size_t sec_len, kg2_osd_head_t *out) {
  if (sec_len < KG2_OSD_HEAD_SIZE) return false;
  out->count = kg_le16(head);
  out->item_size = kg_le16(head + 2);
  return out->item_size >= KG2_ITEM_SIZE &&
         KG2_OSD_HEAD_SIZE + (size_t) out->count * out->item_size <= sec_len;
}

bool kg2_item_record_parse(const uint8_t *r, size_t len, kg2_item_t *out) {
  if (len < KG2_ITEM_SIZE) return false;
  out->type = r[0];
  out->flags = r[1];
  out->index = r[2];
  out->page = r[3];
  out->open_page = r[4];
  out->cond_item = r[5];
  out->cond_flags = r[6];
  out->cond_value = r[7];
  out->def = r[8];
  out->slot = r[9];
  out->keys[0] = r[10];
  out->keys[1] = r[11];
  out->options = r[12];
  out->files = r[13];
  out->key = kg_le16(r + 16);
  out->name = kg_le16(r + 18);
  out->hotkey = kg_le16(r + 20);
  out->exts = kg_le16(r + 22);
  out->dir = kg_le16(r + 24);
  out->options_ofs = kg_le16(r + 26);
  out->files_ofs = kg_le16(r + 28);
  return true;
}

bool kg2_item_parse(const uint8_t *sec, size_t len, const kg2_osd_head_t *head, uint16_t n, kg2_item_t *out) {
  if (n >= head->count) return false;
  size_t at = KG2_OSD_HEAD_SIZE + (size_t) n * head->item_size;
  if (at + KG2_ITEM_SIZE > len || !kg2_item_record_parse(sec + at, len - at, out)) return false;
  // lists out of the section are dropped
  if (out->options && (size_t) out->options_ofs + (size_t) KG2_OPTION_SIZE * out->options > len) out->options = 0;
  if (out->files && (size_t) out->files_ofs + 2u * out->files > len) out->files = 0;
  return true;
}

bool kg2_option_record_parse(const uint8_t *r, size_t len, kg2_option_t *out) {
  if (len < KG2_OPTION_SIZE) return false;
  out->key = kg_le16(r);
  out->name = kg_le16(r + 2);
  out->value = r[4];
  return true;
}

bool kg2_option_parse(const uint8_t *sec, size_t len, uint16_t ofs, uint8_t n, kg2_option_t *out) {
  size_t at = (size_t) ofs + (size_t) KG2_OPTION_SIZE * n;
  if (at + KG2_OPTION_SIZE > len) return false;
  return kg2_option_record_parse(sec + at, len - at, out);
}

bool kg2_rom_block_parse(const uint8_t *hdr, uint32_t remaining, kg2_rom_block_t *out) {
  uint32_t len = kg_le32(hdr);
  out->external = (len & 0x80000000u) != 0;
  out->len = len & 0x7FFFFFFFu;
  out->addr = kg_le32(hdr + 4);
  if (remaining < KG2_ROM_BLOCK_HEADER_SIZE) return false;
  return out->len <= remaining - KG2_ROM_BLOCK_HEADER_SIZE;
}
