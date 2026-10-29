#!/usr/bin/env python3
"""Generates library/symbols/*.json, the built-in device library.

Geometry follows CircuitMaker 2000 proportions scaled by 10/3 (its 3-unit grid
maps onto our 10-unit grid), so imported .ckt schematics keep their layout.
Only pin positions were measured from CircuitMaker files; all drawings are our
own. "cmPins" lists our pin name for CircuitMaker pin 1, 2, ... of the device
names in "aliases".

Run from the repository root:  python3 tools/gen_library.py
"""
import json
import os

OUT = os.path.join(os.path.dirname(__file__), '..', 'library', 'symbols')


def L(*v): return {'line': list(v)}
def R(*v, fill=False): return {'rect': list(v), **({'fill': True} if fill else {})}
def C(*v, fill=False): return {'circle': list(v), **({'fill': True} if fill else {})}
def P(d): return {'path': d}
def T(t, x, y, size=8, align=None): return {'text': t, 'at': [x, y], 'size': size, **({'align': align} if align else {})}
def pin(name, x, y, d='passive', **kw): return {'name': name, 'at': [x, y], 'dir': d, **kw}


def letters(n): return [chr(ord('A') + i) for i in range(n)]


def gate(id, name, op, n, inv, aliases=(), desc='', **extra):
    s = {'id': id, 'name': name, 'shape': {'type': 'gate', 'op': op, 'inputs': n, 'invert': inv},
         'aliases': list(aliases), 'cmPins': letters(n) + ['Y']}
    if desc: s['description'] = desc
    s.update(extra)
    return s


def box(id, name, left=(), right=(), top=(), bottom=(), width=80, pitch=40, title=None, **extra):
    shape = {'type': 'box', 'width': width, 'pitch': pitch, 'left': list(left), 'right': list(right),
             'top': list(top), 'bottom': list(bottom)}
    if title: shape['title'] = title
    s = {'id': id, 'name': name, 'shape': shape}
    s.update(extra)
    return s


def clk(name): return {'name': name, 'clock': True}


def write(fname, category, symbols):
    with open(os.path.join(OUT, fname), 'w') as f:
        json.dump({'category': category, 'symbols': symbols}, f, indent=1, ensure_ascii=False)
        f.write('\n')


GATE_KINDS = {  # id: (op, inputs, invert, display name)
    'inv': ('buf', 1, True, 'Inverter'), 'buf': ('buf', 1, False, 'Buffer'),
    'and2': ('and', 2, False, '2-In AND'), 'and3': ('and', 3, False, '3-In AND'),
    'and4': ('and', 4, False, '4-In AND'), 'and8': ('and', 8, False, '8-In AND'),
    'nand2': ('and', 2, True, '2-In NAND'), 'nand3': ('and', 3, True, '3-In NAND'),
    'nand4': ('and', 4, True, '4-In NAND'), 'nand8': ('and', 8, True, '8-In NAND'),
    'or2': ('or', 2, False, '2-In OR'), 'or3': ('or', 3, False, '3-In OR'),
    'or4': ('or', 4, False, '4-In OR'), 'or8': ('or', 8, False, '8-In OR'),
    'nor2': ('or', 2, True, '2-In NOR'), 'nor3': ('or', 3, True, '3-In NOR'),
    'nor4': ('or', 4, True, '4-In NOR'), 'nor8': ('or', 8, True, '8-In NOR'),
    'xor2': ('xor', 2, False, '2-In XOR'), 'xnor2': ('xor', 2, True, '2-In XNOR'),
}
CM_GATE_ALIASES = {'nor2': ['2-In NOR:DM'], 'nand2': ['2-In NAND:DM']}


def generic_gates():
    out = []
    for id, (op, n, inv, name) in GATE_KINDS.items():
        out.append(gate(id, name, op, n, inv, [name] + CM_GATE_ALIASES.get(id, [])))
    tri_graphics = [L(0, 15, 0, 40)]
    out.append({'id': 'tribuf', 'name': 'Buffer 3S', 'shape': {'type': 'gate', 'op': 'buf', 'inputs': 1},
                'model': {'kind': 'tribuf'}, 'aliases': ['Buffer 3S'], 'cmPins': ['A', 'EN', 'Y'],
                'description': 'Tri-state buffer, active-high enable',
                'graphics': tri_graphics, 'pins': [pin('EN', 0, 40, 'in')]})
    out.append({'id': 'tribuf_n', 'name': 'Buffer 3S (EN low)', 'shape': {'type': 'gate', 'op': 'buf', 'inputs': 1},
                'model': {'kind': 'tribuf'}, 'description': 'Tri-state buffer, active-low enable',
                'graphics': [L(0, 25, 0, 40), C(0, 20, 5)], 'pins': [pin('~EN', 0, 40, 'in')]})
    return out


# package gates: (number, gate kind, count per package, description)
TTL_GATES = [
    ('7400', 'nand2', 4), ('7402', 'nor2', 4), ('7404', 'inv', 6), ('7407', 'buf', 6), ('7408', 'and2', 4),
    ('7410', 'nand3', 3), ('7411', 'and3', 3), ('7415', 'and3', 3), ('7420', 'nand4', 2), ('7421', 'and4', 2),
    ('7422', 'nand4', 2), ('7427', 'nor3', 3), ('7430', 'nand8', 1), ('7432', 'or2', 4), ('7486', 'xor2', 4),
    ('74266', 'xnor2', 4),
]
CMOS_GATES = [
    ('4001', 'nor2', 4), ('4002', 'nor4', 2), ('4011', 'nand2', 4), ('4012', 'nand4', 2), ('4023', 'nand3', 3),
    ('4025', 'nor3', 3), ('4030', 'xor2', 4), ('4049', 'inv', 6), ('4050', 'buf', 6), ('4068', 'and8', 1),
    ('4069', 'inv', 6), ('4070', 'xor2', 4), ('4071', 'or2', 4), ('4072', 'or4', 2), ('4073', 'and3', 3),
    ('4075', 'or3', 3), ('4077', 'xnor2', 4), ('4078', 'nor8', 1), ('4081', 'and2', 4), ('4082', 'and4', 2),
]
COUNT = {1: 'Single', 2: 'Dual', 3: 'Triple', 4: 'Quad', 6: 'Hex'}


def package_gates(parts, ttl):
    out = []
    for number, kind, count in parts:
        op, n, inv, gname = GATE_KINDS[kind]
        aliases = [number] + ([f'74LS{number[2:]}', f'74HC{number[2:]}'] if ttl else [f'CD{number}'])
        name = f'{number} {COUNT[count]} {gname}'
        extra = {'label': number}
        if ttl:
            extra['floatInputs'] = 1
        out.append(gate(number, name, op, n, inv, aliases,
                        f'{COUNT[count]} {gname} ({"TTL" if ttl else "CMOS"}); one gate per symbol', **extra))
    return out


def flipflops():
    Qn = '~Q'
    ff = []
    ff.append(box('dff', 'D Flip-Flop', ['D', clk('CLK')], ['Q', Qn], model={'kind': 'dff'},
                  aliases=['D Flip-Flop'], cmPins=['D', 'CLK', '~Q', 'Q'], description='Positive-edge D flip-flop'))
    ff.append(box('dff_sr', '4013 D Flip-Flop', ['D', clk('CLK')], ['Q', Qn], ['S'], ['R'], model={'kind': 'dff'},
                  aliases=['4013', 'CD4013'], cmPins=['S', 'D', 'CLK', 'R', '~Q', 'Q'], label='4013',
                  description='Positive-edge D flip-flop, active-high set/reset (CD4013, one of two)'))
    ff.append(box('dff_pc', '7474 D Flip-Flop', ['D', clk('CLK')], ['Q', Qn], ['~PRE'], ['~CLR'],
                  model={'kind': 'dff'}, aliases=['7474', '74LS74', '74HC74'], label='7474', floatInputs=1,
                  cmPins=['~PRE', 'D', 'CLK', '~CLR', '~Q', 'Q'],
                  description='Positive-edge D flip-flop, active-low preset/clear (74x74, one of two)'))
    ff.append(box('jkff', 'JK Flip-Flop', ['J', clk('CLK'), 'K'], ['Q', Qn], model={'kind': 'jkff'},
                  aliases=['JK Flip-Flop'], cmPins=['J', 'CLK', 'K', '~Q', 'Q'], description='Positive-edge JK flip-flop'))
    ff.append(box('jkff_sr', '4027 JK Flip-Flop', ['J', clk('CLK'), 'K'], ['Q', Qn], ['S'], ['R'],
                  model={'kind': 'jkff'}, aliases=['4027', 'CD4027'], label='4027',
                  cmPins=['S', 'J', 'CLK', 'K', 'R', '~Q', 'Q'],
                  description='Positive-edge JK flip-flop, active-high set/reset (CD4027, one of two)'))
    ff.append(box('jkff_pcn', '74112 JK Flip-Flop', ['J', clk('~CLK'), 'K'], ['Q', Qn], ['~PRE'], ['~CLR'],
                  model={'kind': 'jkff'}, aliases=['74112', '74LS112', '7476', '74LS76'], label='74112', floatInputs=1,
                  cmPins=['~PRE', 'J', '~CLK', 'K', '~CLR', '~Q', 'Q'],
                  description='Negative-edge JK flip-flop, active-low preset/clear (74x112, one of two)'))
    ff.append(box('jkff_rn', '7473 JK Flip-Flop', ['J', clk('~CLK'), 'K'], ['Q', Qn], [], ['~CLR'],
                  model={'kind': 'jkff'}, aliases=['JK RN', '7473', '74LS73', '74107'], label='7473', floatInputs=1,
                  cmPins=['J', '~CLK', 'K', '~CLR', '~Q', 'Q'],
                  description='Negative-edge JK flip-flop, active-low clear (74x73, one of two)'))
    return ff


def msi():
    def D(n, p='D'): return [f'{p}{i}' for i in range(n)]
    chips = [
        ('7490', 'Decade counter (÷2 and ÷5)', [clk('~CKA'), clk('~CKB'), 'R01', 'R02', 'R91', 'R92'],
         ['QA', 'QB', 'QC', 'QD'], {'kind': 'ripple', 'modB': 5}),
        ('7493', '4-bit binary ripple counter (÷2 and ÷8)', [clk('~CKA'), clk('~CKB'), 'R01', 'R02'],
         ['QA', 'QB', 'QC', 'QD'], {'kind': 'ripple', 'modB': 8}),
        ('74161', 'Synchronous 4-bit counter, asynchronous clear', D(4) + ['ENP', 'ENT', clk('CLK'), '~LOAD', '~CLR'],
         D(4, 'Q') + ['RCO'], {'kind': 'sync'}),
        ('74163', 'Synchronous 4-bit counter, synchronous clear', D(4) + ['ENP', 'ENT', clk('CLK'), '~LOAD', '~CLR'],
         D(4, 'Q') + ['RCO'], {'kind': 'sync', 'syncClear': True}),
        ('74192', 'Up/down decade counter', D(4) + [clk('UP'), clk('DOWN'), '~LOAD', 'CLR'],
         D(4, 'Q') + ['~CO', '~BO'], {'kind': 'updown', 'modulus': 10}),
        ('74193', 'Up/down 4-bit binary counter', D(4) + [clk('UP'), clk('DOWN'), '~LOAD', 'CLR'],
         D(4, 'Q') + ['~CO', '~BO'], {'kind': 'updown', 'modulus': 16}),
        ('74138', '3-to-8 line decoder/demultiplexer', ['A', 'B', 'C', 'G1', '~G2A', '~G2B'],
         [f'~Y{i}' for i in range(8)], {'kind': 'decoder'}),
        ('74139', '2-to-4 line decoder (one of two)', ['A', 'B', '~G'], [f'~Y{i}' for i in range(4)], {'kind': 'decoder'}),
        ('7442', 'BCD-to-decimal decoder', ['A', 'B', 'C', 'D'], [f'~Y{i}' for i in range(10)], {'kind': 'decoder'}),
        ('7447', 'BCD to 7-segment decoder/driver, active-low outputs (common anode)',
         ['A', 'B', 'C', 'D', '~LT', '~BI', '~RBI'], ['~' + c for c in 'abcdefg'], {'kind': 'bcd7seg'}),
        ('7448', 'BCD to 7-segment decoder, active-high outputs (common cathode)',
         ['A', 'B', 'C', 'D', '~LT', '~BI', '~RBI'], list('abcdefg'), {'kind': 'bcd7seg'}),
        ('74151', '8-to-1 multiplexer', D(8) + ['A', 'B', 'C', '~G'], ['Y', '~W'], {'kind': 'mux'}),
        ('74153', '4-to-1 multiplexer (one of two)', D(4) + ['A', 'B', '~G'], ['Y'], {'kind': 'mux'}),
        ('74157', 'Quad 2-to-1 multiplexer', D(4, 'A') + D(4, 'B') + ['S', '~G'], D(4, 'Y'), {'kind': 'mux2'}),
        ('7483', '4-bit binary full adder', D(4, 'A') + D(4, 'B') + ['CI'], D(4, 'S') + ['CO'], {'kind': 'adder'}),
        ('74283', '4-bit binary full adder, fast carry', D(4, 'A') + D(4, 'B') + ['CI'], D(4, 'S') + ['CO'], {'kind': 'adder'}),
        ('7485', '4-bit magnitude comparator', D(4, 'A') + D(4, 'B') + ['I<', 'I=', 'I>'], ['A<B', 'A=B', 'A>B'],
         {'kind': 'comparator'}),
        ('74175', 'Quad D flip-flop with clear', D(4) + [clk('CLK'), '~CLR'], D(4, 'Q') + D(4, '~Q'), {'kind': 'register'}),
        ('74273', 'Octal D flip-flop with clear', D(8) + [clk('CLK'), '~CLR'], D(8, 'Q'), {'kind': 'register'}),
        ('74374', 'Octal D flip-flop, 3-state outputs', D(8) + [clk('CLK'), '~OE'], D(8, 'Q'), {'kind': 'register'}),
        ('74373', 'Octal transparent latch, 3-state outputs', D(8) + ['LE', '~OE'], D(8, 'Q'), {'kind': 'register'}),
        ('74164', '8-bit serial-in parallel-out shift register', ['A', 'B', clk('CLK'), '~CLR'], D(8, 'Q'), {'kind': 'shift'}),
        ('74194', '4-bit bidirectional universal shift register', ['SR'] + D(4) + ['SL', 'S0', 'S1', clk('CLK'), '~CLR'],
         D(4, 'Q'), {'kind': 'shift'}),
        ('74244', 'Octal buffer, 3-state', D(8, 'A') + ['~1G', '~2G'], D(8, 'Y'),
         {'kind': 'tribus', 'groups': [{'enable': '1G', 'bits': [0, 1, 2, 3]}, {'enable': '2G', 'bits': [4, 5, 6, 7]}]}),
        ('74125', 'Quad buffer, 3-state, active-low enables (shared here)', D(4, 'A') + ['~G'], D(4, 'Y'),
         {'kind': 'tribus', 'groups': [{'enable': 'G', 'bits': [0, 1, 2, 3]}]}),
    ]
    out = []
    for number, desc, left, right, model in chips:
        out.append(box(number, f'{number} {desc.split(",")[0]}', left, right, width=120, pitch=20,
                       title=number, model=model, aliases=[number, f'74LS{number[2:]}', f'74HC{number[2:]}'],
                       label=number, description=desc, floatInputs=1))
    return out


def inputs():
    return [
        {'id': 'logicswitch', 'name': 'Logic Switch', 'prefix': 'S', 'display': 'logicswitch',
         'aliases': ['Logic Switch'], 'cmPins': ['Y'], 'model': {'kind': 'logicswitch'}, 'props': {'state': 0},
         'description': 'Click during simulation to toggle 0/1',
         'graphics': [R(-30, -20, 50, 40), L(20, 0, 40, 0)], 'pins': [pin('Y', 40, 0, 'out')]},
        {'id': 'pulser', 'name': 'Pulser', 'prefix': 'P', 'display': 'pulser', 'aliases': ['Pulser'],
         'cmPins': ['', '', 'Q', '~Q'], 'model': {'kind': 'pulser'}, 'props': {'high': 10, 'low': 10},
         'description': 'Clock source; high and low durations in ticks',
         'graphics': [R(-40, -30, 80, 60),
                      P('M -30 10 L -20 10 L -20 -10 L -5 -10 L -5 10 L 10 10 L 10 -10 L 25 -10 L 25 10 L 30 10'),
                      L(40, -20, 70, -20), L(40, 20, 50, 20), C(55, 20, 5), L(60, 20, 70, 20),
                      T('Q', 34, -20, 7, 'right')],
         'pins': [pin('Q', 70, -20, 'out'), pin('~Q', 70, 20, 'out')]},
        {'id': 'hexkey', 'name': 'Hex Key', 'prefix': 'K', 'display': 'hexkey', 'aliases': ['Hex Key'],
         'cmPins': ['D0', 'D1', 'D2', 'D3'], 'model': {'kind': 'key', 'bits': 4}, 'props': {'value': 0},
         'description': 'Click a key during simulation to output its 4-bit value',
         'graphics': [R(-40, -60, 80, 110)] + [L(x, 50, x, 80) for x in (30, 10, -10, -30)],
         'pins': [pin(f'D{i}', 30 - 20 * i, 80, 'out') for i in range(4)]},
        {'id': 'asciikey', 'name': 'Ascii Key', 'prefix': 'K', 'display': 'asciikey', 'aliases': ['Ascii Key'],
         'cmPins': [f'D{i}' for i in range(7)] + ['STB'], 'model': {'kind': 'key', 'bits': 7}, 'props': {'value': 65},
         'description': 'Click during simulation and type a character to output its 7-bit code; STB pulses',
         'graphics': [R(-80, -40, 160, 90)] + [L(x, 50, x, 80) for x in range(70, -71, -20)],
         'pins': [pin(f'D{i}', 70 - 20 * i, 80, 'out') for i in range(7)] + [pin('STB', -70, 80, 'out')]},
        {'id': 'pb_no', 'name': 'NO PushButton', 'prefix': 'S', 'display': 'button', 'aliases': ['NO PushButton'],
         'cmPins': ['1', '2'], 'model': {'kind': 'link', 'type': 'no'},
         'description': 'Normally open; closed while the mouse button is held',
         'graphics': [L(-60, 30, -30, 30), L(30, 30, 60, 30), C(-25, 30, 5), C(25, 30, 5),
                      L(-30, 15, 30, 15), L(0, 15, 0, -5), L(-12, -5, 12, -5)],
         'pins': [pin('2', -60, 30), pin('1', 60, 30)]},
        {'id': 'pb_nc', 'name': 'NC PushButton', 'prefix': 'S', 'display': 'button', 'aliases': ['NC PushButton'],
         'cmPins': ['1', '2'], 'model': {'kind': 'link', 'type': 'nc'},
         'description': 'Normally closed; open while the mouse button is held',
         'graphics': [L(-60, 0, -30, 0), L(30, 0, 60, 0), C(-25, 0, 5), C(25, 0, 5),
                      L(-30, 8, 30, 8), L(0, 8, 0, -25), L(-12, -25, 12, -25)],
         'pins': [pin('2', -60, 0), pin('1', 60, 0)]},
        {'id': 'pb_spdt', 'name': 'SPDT PB', 'prefix': 'S', 'display': 'button', 'aliases': ['SPDT PB'],
         'cmPins': ['C', 'NC', 'NO'], 'model': {'kind': 'link', 'type': 'spdt'},
         'description': 'Change-over push button: C-NC at rest, C-NO while held',
         'graphics': [L(40, 0, 70, 0), C(35, 0, 5), L(-60, 0, -30, 0), C(-25, 0, 5), L(-60, 50, -30, 50),
                      C(-25, 50, 5), L(30, 3, -20, 8), T('NC', -40, -10, 7), T('NO', -40, 38, 7)],
         'pins': [pin('C', 70, 0), pin('NC', -60, 0), pin('NO', -60, 50)]},
        {'id': 'spst', 'name': 'SPST Switch', 'prefix': 'S', 'display': 'spst', 'aliases': ['SPST Switch'],
         'cmPins': ['1', '2'], 'model': {'kind': 'link', 'type': 'toggle'}, 'props': {'closed': 0},
         'description': 'Click during simulation to open/close',
         'graphics': [L(-60, 0, -30, 0), L(30, 0, 60, 0), C(-25, 0, 5), C(25, 0, 5)],
         'pins': [pin('2', -60, 0), pin('1', 60, 0)]},
    ]


def seg_graphics():
    # segments a..g drawn by the display renderer; outline and pins here
    g = [R(-80, -80, 160, 150)]
    names = list('abcdefg') + ['dp']
    for i, n in enumerate(names):
        x = -70 + 20 * i
        g += [L(x, 70, x, 100), T(n, x + 6, 92, 6, 'left')]
    g += [L(0, -80, 0, -100), T('COM', 12, -92, 6, 'left')]
    pins = [pin(n, -70 + 20 * i, 100, 'in') for i, n in enumerate(names)] + [pin('COM', 0, -100, 'in')]
    return g, pins


def outputs():
    seg_g, seg_p = seg_graphics()
    return [
        {'id': 'logicdisplay', 'name': 'Logic Display', 'prefix': 'L', 'display': 'lamp', 'aliases': ['Logic Display'],
         'cmPins': ['D0'], 'model': {'kind': 'display', 'bits': 1},
         'description': 'Lamp showing the logic level of its input',
         'graphics': [C(0, 0, 20), L(0, 20, 0, 60)], 'pins': [pin('D0', 0, 60, 'in')]},
        {'id': 'probe', 'name': 'Logic Probe', 'prefix': 'P', 'display': 'lamp', 'aliases': ['Logic Probe'],
         'cmPins': ['D0'], 'model': {'kind': 'display', 'bits': 1},
         'graphics': [P('M -15 -40 L 15 -40 L 15 -10 L 0 10 L -15 -10 Z'), L(0, 10, 0, 30)],
         'pins': [pin('D0', 0, 30, 'in')]},
        {'id': 'hexdisplay', 'name': 'Hex Display', 'prefix': 'D', 'display': 'hex', 'aliases': ['Hex Display'],
         'cmPins': ['D0', 'D1', 'D2', 'D3'], 'model': {'kind': 'display', 'bits': 4},
         'description': 'Shows the hexadecimal value of D3..D0',
         'graphics': [R(-50, -70, 100, 120)] + [L(x, 50, x, 80) for x in (30, 10, -10, -30)],
         'pins': [pin(f'D{i}', 30 - 20 * i, 80, 'in') for i in range(4)]},
        {'id': 'asciidisplay', 'name': 'Ascii Display', 'prefix': 'D', 'display': 'ascii', 'aliases': ['Ascii Display'],
         'cmPins': [f'D{i}' for i in range(8)], 'model': {'kind': 'display', 'bits': 7},
         'description': 'Shows the character whose 7-bit code is on D6..D0',
         'graphics': [R(-80, -120, 160, 250)] + [L(-110, -100 + 30 * i, -80, -100 + 30 * i) for i in range(8)]
                     + [T(f'D{i}', -96, -106 + 30 * i, 6) for i in range(8)],
         'pins': [pin(f'D{i}', -110, -100 + 30 * i, 'in') for i in range(8)]},
        {'id': 'led', 'name': 'LED', 'prefix': 'D', 'display': 'led', 'aliases': ['LED'], 'cmPins': ['A', 'K'],
         'model': {'kind': 'led'}, 'description': 'Lights when the anode is high and the cathode low',
         'graphics': [L(0, -30, 0, -12), P('M -14 -12 L 14 -12 L 0 12 Z'), L(-14, 12, 14, 12), L(0, 12, 0, 30),
                      L(18, -8, 30, -20), L(22, 4, 34, -8)],
         'pins': [pin('A', 0, -30, 'in'), pin('K', 0, 30, 'in')]},
        {'id': 'stoplight', 'name': 'StopLight', 'prefix': 'L', 'display': 'stoplight', 'aliases': ['StopLight'],
         'cmPins': ['D0', 'D1', 'D2'], 'model': {'kind': 'display', 'bits': 3},
         'description': 'Traffic light: D0 red, D1 yellow, D2 green',
         'graphics': [R(-30, -75, 60, 150), C(0, -50, 18), C(0, 0, 18), C(0, 50, 18),
                      L(-60, -50, -30, -50), L(-60, 0, -30, 0), L(-60, 50, -30, 50)],
         'pins': [pin('D0', -60, -50, 'in'), pin('D1', -60, 0, 'in'), pin('D2', -60, 50, 'in')]},
        {'id': 'seg7_cc', 'name': 'CC 7-Seg', 'prefix': 'D', 'display': 'seg7', 'aliases': ['CC 7-Seg'],
         'model': {'kind': 'seg7'}, 'description': 'Common-cathode 7-segment display: segment lit when its pin is high',
         'graphics': seg_g, 'pins': seg_p},
        {'id': 'seg7_ca', 'name': 'CA 7-Seg', 'prefix': 'D', 'display': 'seg7', 'aliases': ['CA 7-Seg'],
         'model': {'kind': 'seg7', 'anode': True}, 'description': 'Common-anode 7-segment display: segment lit when its pin is low',
         'graphics': seg_g, 'pins': seg_p},
    ]


def general():
    return [
        {'id': 'ground', 'name': 'Ground', 'prefix': 'GND', 'aliases': ['Ground'], 'cmPins': ['GND'],
         'model': {'kind': 'const', 'value': 0},
         'graphics': [L(0, -20, 0, 0), L(-20, 0, 20, 0), L(-13, 6, 13, 6), L(-6, 12, 6, 12)],
         'pins': [pin('GND', 0, -20, 'out')]},
        {'id': 'vcc', 'name': '+V', 'prefix': 'V', 'label': '+5V', 'aliases': ['+V', '+5V', 'Vcc'], 'cmPins': ['VCC'],
         'model': {'kind': 'const', 'value': 1},
         'graphics': [L(0, 30, 0, 0), L(-15, 0, 15, 0), T('+5V', 0, -10, 8)], 'pins': [pin('VCC', 0, 30, 'out')]},
        {'id': 'terminal', 'name': 'Terminal', 'prefix': 'T', 'netLabel': True,
         'aliases': ['Terminal', 'Input', 'Output'], 'cmPins': ['1'],
         'description': 'Connection point; terminals with the same label are connected',
         'graphics': [L(0, 30, 0, 8), C(0, 0, 8)], 'pins': [pin('1', 0, 30)]},
        {'id': 'netlabel', 'name': 'Net label', 'prefix': '', 'netLabel': True, 'hidden': True,
         'description': 'Joins its pin to every other net label with the same text',
         'graphics': [C(0, 0, 2, fill=True)], 'pins': [pin('1', 0, 0)]},
        {'id': 'unknown', 'name': 'Unknown device', 'prefix': 'X', 'hidden': True,
         'description': 'Placeholder for an imported device that has no equivalent in the library',
         'graphics': [R(-40, -30, 80, 60), T('?', 0, 0, 24)], 'pins': []},
        {'id': 'resistor', 'name': 'Resistor', 'prefix': 'R', 'label': '1k', 'aliases': ['Resistor', 'ResistorWire'],
         'cmPins': ['1', '2'], 'model': {'kind': 'link', 'type': 'resistor'},
         'description': 'In digital mode, a weak (pull-up/pull-down) connection',
         'graphics': [L(-60, 0, -30, 0),
                      P('M -30 0 L -25 -10 L -15 10 L -5 -10 L 5 10 L 15 -10 L 25 10 L 30 0'),
                      L(30, 0, 60, 0)],
         'pins': [pin('1', -60, 0), pin('2', 60, 0)]},
    ]


def quad3state():
    return box('quad3state', 'Quad 3-State', [f'A{i}' for i in range(4)], [f'Y{i}' for i in range(4)],
               bottom=['EN'], width=80, pitch=40, title='3-ST', aliases=['Quad 3-State'],
               cmPins=[f'A{i}' for i in range(4)] + [f'Y{i}' for i in range(4)] + ['EN'],
               model={'kind': 'tribus', 'groups': [{'enable': 'EN', 'bits': [0, 1, 2, 3]}]},
               description='Four tri-state buffers with a shared active-high enable')


if __name__ == '__main__':
    for f in os.listdir(OUT):
        if f.endswith('.json'):
            os.remove(os.path.join(OUT, f))
    write('10_general.json', 'General', general())
    write('20_gates.json', 'Digital/Gates', generic_gates() + [quad3state()])
    write('30_flipflops.json', 'Digital/Flip-Flops', flipflops())
    write('40_inputs.json', 'Digital/Inputs', inputs())
    write('50_outputs.json', 'Digital/Outputs', outputs())
    write('60_ttl_gates.json', 'Digital/74xx Gates', package_gates(TTL_GATES, ttl=True))
    write('70_ttl_msi.json', 'Digital/74xx MSI', msi())
    write('80_cmos_gates.json', 'Digital/40xx CMOS', package_gates(CMOS_GATES, ttl=False))
    print('library written to', os.path.normpath(OUT))
