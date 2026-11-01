"""Simulate encoder.pio: direction, bounce, invalid states and full RX FIFO."""
from pathlib import Path

program, labels = [], {}
wrap = 0
for raw in (Path(__file__).resolve().parents[1] / 'pio' / 'encoder.pio').read_text().splitlines():
    line = raw.split(';')[0].strip().lower()
    if line == '.wrap_target':
        wrap = len(program)
    elif not line or line.startswith('.'):
        continue
    elif line.endswith(':'):
        labels[line.removeprefix('public ')[:-1]] = len(program)
    else:
        program.append(line)
assert len(program) <= 32

class Machine:
    def __init__(self, pins=0, count=0):
        self.pins, self.y, self.osr = pins, count, pins
        self.isr, self.pc, self.fifo = 0, wrap, []

    def run(self, pins, cycles=50):
        self.pins = pins
        for _ in range(cycles):
            op = program[self.pc]
            next_pc = self.pc + 1
            if op.startswith('jmp y-- '):
                previous = self.y
                self.y = (self.y - 1) & 0xffffffff
                if previous:
                    next_pc = labels[op.split()[-1]]
            elif op.startswith('jmp '):
                next_pc = labels[op.split()[-1]]
            elif op == 'mov isr, y':
                self.isr = self.y
            elif op == 'push noblock':
                if len(self.fifo) < 8:
                    self.fifo.append(self.isr)
                self.isr = 0
            elif op == 'out isr, 2':
                self.isr, self.osr = self.osr & 3, self.osr >> 2
            elif op == 'in pins, 2':
                self.isr = ((self.isr << 2) | self.pins) & 0xffffffff
            elif op == 'mov osr, isr':
                self.osr = self.isr
            elif op == 'mov pc, isr':
                next_pc = self.isr
            elif op == 'mov y, ~y':
                self.y = (~self.y) & 0xffffffff
            else:
                raise AssertionError(op)
            self.pc = wrap if next_pc == len(program) else next_pc

for initial in range(4):
    machine = Machine(initial)
    machine.run(initial)
    assert machine.y == 0, 'startup must not invent an edge'

machine = Machine()
for _ in range(100):
    for pins in (1, 3, 2, 0):
        machine.run(pins)
assert machine.y == 400, 'full FIFO must not stop counting'
assert len(machine.fifo) == 8
machine.fifo.clear()
machine.run(0)
assert machine.fifo[-1] == 400, 'fresh snapshot restores all accumulated motion'
for _ in range(100):
    for pins in (2, 3, 1, 0):
        machine.run(pins)
assert machine.y == 0
for pins in (3, 0):
    machine.run(pins)
assert machine.y == 0, 'illegal two-bit transitions must not count'
for pins in (1, 0, 1, 3, 2, 0):
    machine.run(pins)
assert machine.y == 4, 'contact bounce should reverse its count'
machine = Machine(count=0xfffffffe)
for pins in (1, 3, 2, 0):
    machine.run(pins)
assert machine.y == 2
print('Encoder PIO direction, startup, bounce, FIFO and wrap tests passed')
