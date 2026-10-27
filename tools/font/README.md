# OSD fonts

Hand-drawn 8x8 fonts of the text OSD, 256 characters, 2048 bytes each.

- `osd_font_866.pf`: the font in use (code page 866). `src/ui/osd_font.h` is made from it with
  `bin2c` of hxtools; the firmware sends it to the core at every core load (`core_loader.c`).
- `osd_font.pf`, `osd_font_small.pf`: earlier variants, kept for reference.
