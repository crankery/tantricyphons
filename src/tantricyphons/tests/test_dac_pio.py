"""Check PIO frame length and MCP4821 wire protocol without hardware.

Run: python3 tests/test_dac_pio.py
"""
from pathlib import Path
import re

source = Path(__file__).resolve().parents[1] / 'pio' / 'dac.pio'
program, labels = [], {}
for raw in source.read_text().splitlines():
    line = raw.split(';')[0].strip()
    if not line or line.startswith('.'):
        continue
    if line.endswith(':'):
        labels[line[:-1]] = len(program)
    else:
        match = re.fullmatch(r'(.+?)\s+side\s+(\d+)(?:\s+\[(\d+)\])?', line)
        assert match, line
        program.append((match[1].strip(), int(match[2]), int(match[3] or 0)))

for word in (0x3000, 0x3800, 0x3fff, 0x3555, 0x3aaa):
    pc = cycles = x = 0
    osr = 0
    sdi, ldac, previous_side = 0, 1, 1
    sampled, latch_edges = [], []
    trace = []
    while True:
        op, side, delay = program[pc]
        next_pc = pc + 1
        if op == 'pull block':
            osr = word << 16  # 16-bit DMA writes replicate; top half shifts first
        elif op.startswith('set x,'):
            x = int(op.split(',')[1])
        elif op == 'out pins, 1':
            sdi = (osr >> 31) & 1
            osr = (osr << 1) & 0xffffffff
        elif op.startswith('set pins,'):
            new_ldac = int(op.split(',')[1])
            if ldac and not new_ldac:
                assert side & 1, 'LDAC must fall after CS rises'
                latch_edges.append(cycles)
            ldac = new_ldac
        elif op.startswith('jmp x-- '):
            if x:
                next_pc = labels[op.split()[-1]]
            x = (x - 1) & 0xffffffff
        else:
            assert op == 'nop', op
        if side & 2 and not previous_side & 2:
            assert not side & 1, 'SCK rising while CS inactive'
            sampled.append(sdi)
        trace.extend([(side, ldac)] * (delay + 1))
        cycles += delay + 1
        previous_side = side
        pc = next_pc
        if pc == len(program):
            break
    assert cycles == 256, cycles
    assert len(sampled) == 16
    assert int(''.join(map(str, sampled)), 2) == word
    assert len(latch_edges) == 1
    assert sum(ldac == 0 for _, ldac in trace) == 2
    assert trace[-1] == (1, 1), 'Idle must leave CS/LDAC high and SCK low'
print('DAC PIO waveform tests passed')
