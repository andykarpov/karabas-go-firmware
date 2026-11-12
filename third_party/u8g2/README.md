# u8g2 (subset)

The monochrome graphics library of Oliver Kraus, https://github.com/olikraus/u8g2 (BSD 2-clause,
see LICENSE; the fonts have their own terms, listed there), commit d6c8499 of 2026-09-14.

Taken: the u8g2 core of `csrc/` (buffer, fonts, lines, boxes, circles, bitmaps), the u8x8 setup,
display, byte / gpio / cad stubs, capture and number helpers it links against. Not taken: the
display drivers (`u8x8_d_*`, `u8g2_d_*`): the firmware has its own display, the bitmap OSD of
MCU link v2 (`src/ui/osd2.c`, karabas-go-firmware docs/link_v2.md 5.3).

`u8g2_fonts_karabas.c`: the fonts the firmware uses, cut out of `csrc/u8g2_fonts.c` as they are:
`u8g2_font_6x12_t_cyrillic`, `u8g2_font_5x8_t_cyrillic`, `u8g2_font_haxrcorp4089_t_cyrillic`.
