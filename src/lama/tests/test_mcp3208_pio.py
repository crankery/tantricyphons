"""Simulate MCP3208 command and result timing. Run with python3."""
from pathlib import Path
import re

program, labels = [], {}
for raw in (Path(__file__).resolve().parents[1] / 'pio' / 'mcp3208.pio').read_text().splitlines():
    line = raw.split(';')[0].strip()
    if not line or line.startswith('.'):
        continue
    if line.endswith(':'):
        labels[line[:-1]] = len(program)
    else:
        match = re.fullmatch(r'(.+?)\s+side\s+(\d+)(?:\s+\[(\d+)\])?', line)
        assert match, line
        program.append((match[1].strip(), int(match[2]), int(match[3] or 0)))

for channel in range(8):
    for value in (0, 1, 0x800, 0x555, 0xaaa, 0xfff):
        command = (0x060000 | channel << 14) << 8
        pc = cycles = x = isr = 0
        sdi = dout = rising_edges = 0
        previous_side = 1
        bits, high_edges, low_edges = [], [], []
        received = None
        while pc < len(program):
            op, side, delay = program[pc]
            next_pc = pc + 1
            # MCP3208 updates output on falling edges. The falling edge
            # after rising edge 12 presents B11 (datasheet Figure 6-1).
            if previous_side & 2 and not side & 2:
                low_edges.append(cycles)
                if 12 <= rising_edges <= 23:
                    dout = (value >> (23 - rising_edges)) & 1
            if op == 'pull block':
                assert side == 1
                osr = command
            elif op == 'mov isr, null':
                isr = 0
            elif op.startswith('set x,'):
                x = int(op.split(',')[1])
            elif op == 'out pins, 1':
                sdi = (osr >> 31) & 1
                osr = (osr << 1) & 0xffffffff
            elif op == 'in pins, 1':
                isr = ((isr << 1) | dout) & 0xffffffff
            elif op.startswith('jmp x-- '):
                if x:
                    next_pc = labels[op.split()[-1]]
                x = (x - 1) & 0xffffffff
            elif op == 'push block':
                assert side == 1
                received = isr
            else:
                assert op == 'nop'
            if side & 2 and not previous_side & 2:
                assert not side & 1
                high_edges.append(cycles)
                bits.append(sdi)
                rising_edges += 1
            previous_side = side
            cycles += delay + 1
            pc = next_pc
        assert rising_edges == 24
        transmitted = int(''.join(map(str, bits)), 2)
        assert transmitted >> 14 == (0b0000011000 | channel)
        assert received & 0xfff == value, (channel, value, received)
        assert all(b - a == 4 for a, b in zip(high_edges, high_edges[1:]))
        assert all(lo - hi == 2 for hi, lo in zip(high_edges, low_edges))
print('MCP3208 PIO command/response tests passed (all eight channels)')
