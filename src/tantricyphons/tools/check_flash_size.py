"""Keep the last two 4 KiB sectors of the Pico 2 flash outside the image."""
from pathlib import Path
import sys
size = Path(sys.argv[1]).stat().st_size
if size > 4194304 - 8192:
    raise SystemExit('Firmware overlaps reserved settings sectors')
