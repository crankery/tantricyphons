These are the existing monk frames copied from the MicroPython prototype
(`src/lamarama/lamarama/assets`), preserving the original binary format:

- 30 frames, each 120 x 112 pixels, packed 4 bits per pixel.
- High nibble is the left pixel; low nibble is the right pixel.
- `monk-palette.bin` contains 16 RGB565 colors, already in display wire order.
- Total asset payload: 201,632 bytes.

`tools/embed_monk.py` validates sizes and embeds the packed bytes as constant
arrays in generated `build/monk_assets.c`. No expanded frames are stored in
flash. `src/display/monk.c` expands one source row at a time into two 240-pixel rows in RAM.
