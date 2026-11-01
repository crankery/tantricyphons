"""Run clang-format, then separate completed blocks from following statements.

Usage: python3 tools/format_code.py --clang-format /path/to/clang-format
"""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--clang-format', default='clang-format')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
files = sorted(p for p in root.rglob('*') if p.suffix in ('.c', '.h')
               and not {'build', '.git'}.intersection(p.relative_to(root).parts))
subprocess.run([args.clang_format, '-i', *map(str, files)], check=True)
for path in files:
    lines = path.read_text().splitlines(keepends=True)
    output = []
    for index, line in enumerate(lines):
        output.append(line)
        if line.strip() != '}' or index + 1 == len(lines):
            continue
        following = lines[index + 1].lstrip()
        # Nested closing braces and block continuations belong together.
        if not following.strip() or following.startswith(('}', ')', ']', ',', ';', '#')):
            continue
        if re.match(r'(?:else|catch)\b', following):
            continue
        # Keep do/while together; a normal following while loop can have a gap.
        if re.match(r'while\s*\(.*\);\s*$', following):
            continue
        output.append('\n')
    path.write_text(''.join(output))
print(f'Formatted {len(files)} C/header files')
