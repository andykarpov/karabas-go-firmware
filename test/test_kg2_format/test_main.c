// kg2_format on fixture.kg, a file made by make_core of karabas-go-tools from fixture/core.json
// (fixture/make_fixture.sh): the boards go and minig, every section type.
#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kg2_format.h"

#define GO 1
#define MINI 2
#define MINIG 3

static uint8_t *file;
static size_t file_len;
static kg2_header_t hdr;

void setUp() {}
void tearDown() {}

static void load_fixture(void) {
  FILE *f = fopen("fixture.kg", "rb");
  if (!f) { fprintf(stderr, "fixture.kg not found\n"); exit(1); }
  fseek(f, 0, SEEK_END);
  file_len = (size_t) ftell(f);
  fseek(f, 0, SEEK_SET);
  file = malloc(file_len);
  if (fread(file, 1, file_len, f) != file_len) exit(1);
  fclose(f);
}

static kg2_layout_t layout(uint8_t hw) {
  kg2_layout_t l;
  TEST_ASSERT_TRUE(kg2_layout_parse(&hdr, file + hdr.header_size, file_len - hdr.header_size, file_len,
                                    KG2_BOARD_BIT(hw), &l));
  return l;
}

static const uint8_t *sec(const kg2_layout_t *l, int type) { return file + l->sec[type].offset; }
static size_t sec_len(const kg2_layout_t *l, int type) { return l->sec[type].length; }

static const char *str(const kg2_layout_t *l, uint16_t ref) {
  return kg2_str(sec(l, KG2_SEC_STRINGS), sec_len(l, KG2_SEC_STRINGS), ref);
}

static void test_header() {
  TEST_ASSERT_TRUE(kg2_header_parse(file, file_len, &hdr));
  TEST_ASSERT_EQUAL(1, hdr.version);
  TEST_ASSERT_EQUAL(KG2_HEADER_SIZE, hdr.header_size);
  TEST_ASSERT_EQUAL(KG2_ENTRY_SIZE, hdr.entry_size);
  TEST_ASSERT_EQUAL_HEX16(KG2_BOARD_BIT(GO) | KG2_BOARD_BIT(MINIG), hdr.boards);
  uint8_t bad[KG2_HEADER_SIZE];
  memcpy(bad, file, sizeof(bad));
  bad[3] = '1'; // kgo1
  TEST_ASSERT_FALSE(kg2_header_parse(bad, sizeof(bad), &hdr));
  TEST_ASSERT_FALSE(kg2_header_parse(file, 16, &hdr));
  TEST_ASSERT_TRUE(kg2_header_parse(file, file_len, &hdr));
}

static void test_layout_boards() {
  kg2_layout_t go = layout(GO), minig = layout(MINIG);
  // shared sections: the same offset; per board: different
  TEST_ASSERT_EQUAL(go.sec[KG2_SEC_STRINGS].offset, minig.sec[KG2_SEC_STRINGS].offset);
  TEST_ASSERT_EQUAL(go.sec[KG2_SEC_ROMS].offset, minig.sec[KG2_SEC_ROMS].offset);
  TEST_ASSERT_NOT_EQUAL(go.sec[KG2_SEC_CORE].offset, minig.sec[KG2_SEC_CORE].offset);
  TEST_ASSERT_EQUAL(11, go.sec[KG2_SEC_BITSTREAM].length);
  TEST_ASSERT_EQUAL_MEMORY("GOBITSTREAM", sec(&go, KG2_SEC_BITSTREAM), 11);
  // minig is a core of link v2: its bitstream in BITSTREAM_LINK2 only
  TEST_ASSERT_FALSE(minig.sec[KG2_SEC_BITSTREAM].present);
  TEST_ASSERT_FALSE(go.sec[KG2_SEC_BITSTREAM_LINK2].present);
  TEST_ASSERT_EQUAL_MEMORY("MINIGBITSTREAM!", sec(&minig, KG2_SEC_BITSTREAM_LINK2), 15);
  TEST_ASSERT_FALSE(go.sec[KG2_SEC_MATRIX].present);
  // a board without a bitstream
  kg2_layout_t l;
  TEST_ASSERT_FALSE(kg2_layout_parse(&hdr, file + hdr.header_size, file_len - hdr.header_size, file_len,
                                     KG2_BOARD_BIT(MINI), &l));
  // a file cut short
  TEST_ASSERT_FALSE(kg2_layout_parse(&hdr, file + hdr.header_size, file_len - hdr.header_size, file_len - 1,
                                     KG2_BOARD_BIT(MINIG), &l));
}

static void test_core() {
  kg2_layout_t go = layout(GO), minig = layout(MINIG);
  kg2_core_t c;
  TEST_ASSERT_TRUE(kg2_core_parse(sec(&go, KG2_SEC_CORE), sec_len(&go, KG2_SEC_CORE), &c));
  TEST_ASSERT_EQUAL_STRING("test", str(&go, c.id));
  TEST_ASSERT_EQUAL_STRING("Test Core", str(&go, c.name));
  TEST_ASSERT_EQUAL_STRING("26110401", str(&go, c.build));
  TEST_ASSERT_EQUAL(1, c.type);
  TEST_ASSERT_EQUAL(1, c.visible);
  TEST_ASSERT_EQUAL(7, c.order);
  TEST_ASSERT_EQUAL(1, c.rtc_mode);
  TEST_ASSERT_EQUAL(4, c.eeprom_bank);
  TEST_ASSERT_EQUAL(0, c.spi_freq);
  TEST_ASSERT_EQUAL_STRING("A,B", str(&go, c.drives));
  TEST_ASSERT_EQUAL(1, c.link);
  TEST_ASSERT_TRUE(kg2_core_parse(sec(&minig, KG2_SEC_CORE), sec_len(&minig, KG2_SEC_CORE), &c));
  TEST_ASSERT_EQUAL(16, c.spi_freq);
  TEST_ASSERT_EQUAL(2, c.link);
  TEST_ASSERT_EQUAL_STRING("", str(&minig, c.drives));
}

static void test_strings() {
  kg2_layout_t go = layout(GO);
  TEST_ASSERT_EQUAL_STRING("", str(&go, 0));
  TEST_ASSERT_EQUAL_STRING("", str(&go, 0xFFFF)); // out of the section
  uint8_t s[] = { 0, 'a', 'b' };                  // not terminated
  TEST_ASSERT_EQUAL_STRING("", kg2_str(s, sizeof(s), 1));
}

static void test_loader() {
  kg2_layout_t go = layout(GO);
  kg2_loader_t ld;
  const uint8_t *s = sec(&go, KG2_SEC_LOADER);
  size_t n = sec_len(&go, KG2_SEC_LOADER);
  TEST_ASSERT_TRUE(kg2_loader_parse(s, n, &ld));
  TEST_ASSERT_EQUAL_STRING("/test", str(&go, ld.dir));
  TEST_ASSERT_EQUAL_STRING("tap,tzx", str(&go, ld.exts));
  TEST_ASSERT_EQUAL_STRING("", str(&go, ld.save_dir));
  TEST_ASSERT_EQUAL(0, ld.flags & KG2_LOADER_AUTOLOAD);
  TEST_ASSERT_EQUAL(2, ld.files);
  uint16_t ref;
  TEST_ASSERT_TRUE(kg2_ref_at(s, n, ld.files_ofs, 0, &ref));
  TEST_ASSERT_EQUAL_STRING("missing.tap", str(&go, ref));
  TEST_ASSERT_TRUE(kg2_ref_at(s, n, ld.files_ofs, 1, &ref));
  TEST_ASSERT_EQUAL_STRING("game.tap", str(&go, ref));
  TEST_ASSERT_FALSE(kg2_ref_at(s, n, ld.files_ofs, 2, &ref));
}

static void test_osd() {
  kg2_layout_t go = layout(GO), minig = layout(MINIG);
  const uint8_t *s = sec(&go, KG2_SEC_OSD);
  size_t n = sec_len(&go, KG2_SEC_OSD);
  kg2_osd_head_t head;
  TEST_ASSERT_TRUE(kg2_osd_head_parse(s, n, &head));
  TEST_ASSERT_EQUAL(8, head.count);
  // the firmware reads the 8 bytes of the head alone, with the length of the section
  uint8_t head_only[KG2_OSD_HEAD_SIZE];
  memcpy(head_only, s, sizeof(head_only));
  TEST_ASSERT_TRUE(kg2_osd_head_parse(head_only, n, &head));
  TEST_ASSERT_EQUAL(8, head.count);
  TEST_ASSERT_FALSE(kg2_osd_head_parse(head_only, KG2_OSD_HEAD_SIZE + 47, &head));
  TEST_ASSERT_TRUE(kg2_osd_head_parse(head_only, n, &head));
  kg2_item_t it;

  // the Go variant of the disk: physical, no default file
  TEST_ASSERT_TRUE(kg2_item_parse(s, n, &head, 0, &it));
  TEST_ASSERT_EQUAL(5, it.type);
  TEST_ASSERT_EQUAL_STRING("disk", str(&go, it.key));
  TEST_ASSERT_EQUAL(KG2_ITEM_PHYSICAL, it.flags);
  TEST_ASSERT_EQUAL(255, it.index);
  TEST_ASSERT_EQUAL(1, it.slot);
  TEST_ASSERT_EQUAL_STRING("dsk", str(&go, it.exts));
  TEST_ASSERT_EQUAL_STRING("/test", str(&go, it.dir));
  TEST_ASSERT_EQUAL(0, it.files);
  TEST_ASSERT_EQUAL_HEX8(0x88, it.keys[0]);
  TEST_ASSERT_EQUAL_HEX8(0x3c, it.keys[1]);
  TEST_ASSERT_EQUAL_STRING("Menu+F3", str(&go, it.hotkey));

  // video: index 20, options with values 3 / 5, the default NTSC = 5
  TEST_ASSERT_TRUE(kg2_item_parse(s, n, &head, 1, &it));
  TEST_ASSERT_EQUAL(0, it.type);
  TEST_ASSERT_EQUAL(20, it.index);
  TEST_ASSERT_EQUAL(5, it.def);
  TEST_ASSERT_EQUAL(2, it.options);
  kg2_option_t o;
  TEST_ASSERT_TRUE(kg2_option_parse(s, n, it.options_ofs, 0, &o));
  TEST_ASSERT_EQUAL_STRING("pal", str(&go, o.key));
  TEST_ASSERT_EQUAL_STRING("PAL 50Hz", str(&go, o.name));
  TEST_ASSERT_EQUAL(3, o.value);
  TEST_ASSERT_TRUE(kg2_option_parse(s, n, it.options_ofs, 1, &o));
  TEST_ASSERT_EQUAL(5, o.value);
  TEST_ASSERT_FALSE(kg2_option_parse(s, n, (uint16_t) n, 0, &o));

  // speed: the index of its position, a condition on video (item 1 + 1), value 3, disable + not
  TEST_ASSERT_TRUE(kg2_item_parse(s, n, &head, 2, &it));
  TEST_ASSERT_EQUAL(2, it.index);
  TEST_ASSERT_EQUAL(2, it.cond_item);
  TEST_ASSERT_EQUAL_HEX8(0xC0, it.cond_flags);
  TEST_ASSERT_EQUAL(3, it.cond_value);
  TEST_ASSERT_EQUAL_STRING("speed", str(&go, it.key));

  // the text line keeps its 28 chars
  TEST_ASSERT_TRUE(kg2_item_parse(s, n, &head, 3, &it));
  TEST_ASSERT_EQUAL(4, it.type);
  TEST_ASSERT_EQUAL_STRING("A text line longer than 16 c", str(&go, it.name));

  // the page entry, the trigger on its page
  TEST_ASSERT_TRUE(kg2_item_parse(s, n, &head, 4, &it));
  TEST_ASSERT_EQUAL(7, it.type);
  TEST_ASSERT_EQUAL(1, it.open_page);
  TEST_ASSERT_TRUE(kg2_item_parse(s, n, &head, 5, &it));
  TEST_ASSERT_EQUAL(2, it.type);
  TEST_ASSERT_EQUAL(1, it.page);
  TEST_ASSERT_EQUAL(5, it.index);

  // FL with autoload and two file candidates
  TEST_ASSERT_TRUE(kg2_item_parse(s, n, &head, 6, &it));
  TEST_ASSERT_EQUAL(6, it.type);
  TEST_ASSERT_EQUAL(KG2_ITEM_AUTOLOAD, it.flags);
  TEST_ASSERT_EQUAL(2, it.files);
  uint16_t ref;
  TEST_ASSERT_TRUE(kg2_ref_at(s, n, it.files_ofs, 1, &ref));
  TEST_ASSERT_EQUAL_STRING("/roms/bios.rom", str(&go, ref));

  // a radio item: a switch with the flag, a hotkey per option
  TEST_ASSERT_TRUE(kg2_item_parse(s, n, &head, 7, &it));
  TEST_ASSERT_EQUAL(0, it.type);
  TEST_ASSERT_EQUAL(KG2_ITEM_RADIO, it.flags);
  TEST_ASSERT_EQUAL(1, it.page);
  TEST_ASSERT_EQUAL(2, it.options);
  TEST_ASSERT_EQUAL(0, it.keys[1]);
  TEST_ASSERT_NOT_EQUAL(0, it.option_hotkeys_ofs);
  kg2_option_hotkey_t hk;
  TEST_ASSERT_TRUE(kg2_option_hotkey_parse(s + it.option_hotkeys_ofs, KG2_OPTION_HOTKEY_SIZE, &hk));
  TEST_ASSERT_EQUAL_HEX8(0x88, hk.keys[0]);
  TEST_ASSERT_EQUAL_HEX8(0x3a, hk.keys[1]);
  TEST_ASSERT_EQUAL_STRING("Menu+F1", str(&go, hk.text));
  TEST_ASSERT_TRUE(kg2_option_hotkey_parse(s + it.option_hotkeys_ofs + KG2_OPTION_HOTKEY_SIZE, KG2_OPTION_HOTKEY_SIZE, &hk));
  TEST_ASSERT_EQUAL(0, hk.keys[1]);
  TEST_ASSERT_EQUAL_STRING("", str(&go, hk.text));
  TEST_ASSERT_FALSE(kg2_option_hotkey_parse(s, KG2_OPTION_HOTKEY_SIZE - 1, &hk));
  TEST_ASSERT_FALSE(kg2_item_parse(s, n, &head, 8, &it));

  // the Mini G variant of the disk: read-only, a default file
  s = sec(&minig, KG2_SEC_OSD);
  n = sec_len(&minig, KG2_SEC_OSD);
  TEST_ASSERT_TRUE(kg2_osd_head_parse(s, n, &head));
  TEST_ASSERT_TRUE(kg2_item_parse(s, n, &head, 0, &it));
  TEST_ASSERT_EQUAL(KG2_ITEM_READONLY, it.flags);
  TEST_ASSERT_EQUAL(1, it.files);
  TEST_ASSERT_TRUE(kg2_ref_at(s, n, it.files_ofs, 0, &ref));
  TEST_ASSERT_EQUAL_STRING("a.dsk", str(&minig, ref));

  // a section cut short
  TEST_ASSERT_FALSE(kg2_osd_head_parse(s, KG2_OSD_HEAD_SIZE + 10, &head));
}

static void test_buttons_eeprom() {
  kg2_layout_t go = layout(GO);
  const uint8_t *b = sec(&go, KG2_SEC_BUTTONS);
  TEST_ASSERT_EQUAL(19, sec_len(&go, KG2_SEC_BUTTONS));
  TEST_ASSERT_EQUAL(6, b[0]);
  TEST_ASSERT_EQUAL(0, b[1]);    // btn1 press: core
  TEST_ASSERT_EQUAL(2, b[4]);    // btn2 press: osd
  TEST_ASSERT_EQUAL(3, b[5]);    // hold: menu
  TEST_ASSERT_EQUAL(20, b[6]);   // 2000 ms
  const uint8_t *e = sec(&go, KG2_SEC_EEPROM_INIT);
  TEST_ASSERT_EQUAL(256, sec_len(&go, KG2_SEC_EEPROM_INIT));
  TEST_ASSERT_EQUAL_HEX8(0xAA, e[0]);
  TEST_ASSERT_EQUAL_HEX8(0xBB, e[1]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, e[2]);
}

static void test_roms() {
  kg2_layout_t go = layout(GO);
  const uint8_t *r = sec(&go, KG2_SEC_ROMS);
  uint32_t left = sec_len(&go, KG2_SEC_ROMS);
  kg2_rom_block_t b;
  TEST_ASSERT_TRUE(kg2_rom_block_parse(r, left, &b));
  TEST_ASSERT_FALSE(b.external);
  TEST_ASSERT_EQUAL(4, b.len);
  TEST_ASSERT_EQUAL(4096, b.addr);
  TEST_ASSERT_EQUAL_MEMORY("\x01\x02\x03\x04", r + 8, 4);
  r += 12; left -= 12;
  TEST_ASSERT_TRUE(kg2_rom_block_parse(r, left, &b));
  TEST_ASSERT_TRUE(b.external);
  TEST_ASSERT_EQUAL(13, b.len);
  TEST_ASSERT_EQUAL(65536, b.addr);
  TEST_ASSERT_EQUAL_MEMORY("/roms/ext.rom", r + 8, 13);
  TEST_ASSERT_FALSE(kg2_rom_block_parse(r, left - 1, &b));
}

int main() {
  load_fixture();
  UNITY_BEGIN();
  RUN_TEST(test_header);
  RUN_TEST(test_layout_boards);
  RUN_TEST(test_core);
  RUN_TEST(test_strings);
  RUN_TEST(test_loader);
  RUN_TEST(test_osd);
  RUN_TEST(test_buttons_eeprom);
  RUN_TEST(test_roms);
  free(file);
  return UNITY_END();
}
