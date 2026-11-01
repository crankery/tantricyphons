"""Embed original packed 4bpp assets without expanding their pixels."""
from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
frames = [(source / f'monk-{i:02d}.bin').read_bytes() for i in range(30)]
palette = (source / 'monk-palette.bin').read_bytes()
if any(len(frame) != 6720 for frame in frames) or len(palette) != 32:
    raise SystemExit('Unexpected monk asset dimensions or palette size')

def array(data):
    return ',\n'.join(','.join(f'0x{b:02x}' for b in data[i:i+32]) for i in range(0, len(data), 32))

output.write_text('#include "display/monk.h"\nconst uint8_t monk_palette[32] = {\n' +
                  array(palette) + '\n};\nconst uint8_t monk_frames[30][6720] = {\n' +
                  ',\n'.join('{\n' + array(frame) + '\n}' for frame in frames) + '\n};\n')
