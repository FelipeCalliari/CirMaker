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
    ff = flipflop_symbols()
    # CircuitMaker proportions: data and clock 60 apart (J/CLK/K 30 apart),
    # Q and Q-bar 60 apart, body 100 wide
    for s in ff:
        shape = s['shape']
        if len(shape['left']) == 2:
            shape['left'] = [shape['left'][0], '', shape['left'][1]]
        shape['right'] = [shape['right'][0], '', shape['right'][1]]
        shape.update(pitch=30, width=100, leftStart=-30, rightStart=-30, labelSize=14)
        # CircuitMaker labels the clock input "CP"
        shape['left'] = [dict(e, label='CP') if isinstance(e, dict) and e.get('name', '').endswith('CLK') else e
                         for e in shape['left']]
    return ff


def flipflop_symbols():
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
    ]
    out = []
    for number, desc, left, right, model in chips:
        out.append(box(number, f'{number} {desc.split(",")[0]}', left, right, width=120, pitch=20,
                       title=number, model=model, aliases=[number, f'74LS{number[2:]}', f'74HC{number[2:]}'],
                       label=number, description=desc, floatInputs=1))
    return out


RIGHT_INPUTS = {'SER', '~LD', 'INH', '~CLR', '~CS', '~WE', 'DIR', 'I7', 'I8'}


def cm_chip(number, desc, left, right, model, cm_pins, left_start, right_start, aliases=None, title=None, **extra):
    """MSI chip laid out like CircuitMaker's: pitch 30, pins at x = +-110,
    first pin of each side given explicitly ('' leaves a gap)."""
    # inputs that CircuitMaker draws on the right-hand side
    right = [{'name': e, 'dir': 'in'} if isinstance(e, str) and e in RIGHT_INPUTS else e for e in right]
    shape = {'type': 'box', 'width': 160, 'pitch': 30, 'left': left, 'right': right,
             'leftStart': left_start, 'rightStart': right_start, 'title': title or number}
    names = aliases or [number, f'74LS{number[2:]}', f'74HC{number[2:]}', f'74F{number[2:]}']
    s = {'id': number, 'name': f'{number} {desc.split(",")[0]}', 'shape': shape, 'model': model,
         'aliases': names, 'cmPins': cm_pins, 'label': number, 'description': desc, 'floatInputs': 1}
    s.update(extra)
    return s


def cm_chips():
    D = lambda n, p='D': [f'{p}{i}' for i in range(n)]
    rev = lambda l: list(reversed(l))
    du = {'name': 'D/~U', 'label': 'D/U'}
    chips = [
        cm_chip('74126', 'Quad buffer, 3-state, active-high enables',
                ['C3', 'A3', 'C2', 'A2', 'C1', 'A1', 'C0', 'A0'], ['Y3', '', 'Y2', '', 'Y1', '', 'Y0'],
                {'kind': 'tribus', 'groups': [{'enable': f'C{i}', 'bits': [i]} for i in range(4)]},
                ['C3', 'A3', 'C2', 'A2', 'C1', 'A1', 'C0', 'A0', 'Y3', 'Y2', 'Y1', 'Y0'], -90, -60),
        cm_chip('74125', 'Quad buffer, 3-state, active-low enables',
                ['~C3', 'A3', '~C2', 'A2', '~C1', 'A1', '~C0', 'A0'], ['Y3', '', 'Y2', '', 'Y1', '', 'Y0'],
                {'kind': 'tribus', 'groups': [{'enable': f'C{i}', 'bits': [i]} for i in range(4)]},
                ['~C3', 'A3', '~C2', 'A2', '~C1', 'A1', '~C0', 'A0', 'Y3', 'Y2', 'Y1', 'Y0'], -90, -60),
        cm_chip('74191', 'Synchronous up/down 4-bit binary counter',
                ['~CTEN', clk('CLK'), '~LOAD', du] + rev(D(4)), ['~RCO', 'MAX/MIN'] + rev(D(4, 'Q')),
                {'kind': 'updown1', 'modulus': 16},
                ['~CTEN', 'CLK', '~LOAD', 'D/~U', 'D3', 'D2', 'D1', 'D0', '~RCO', 'MAX/MIN', 'Q3', 'Q2', 'Q1', 'Q0'], -90, -30),
        cm_chip('74190', 'Synchronous up/down decade counter',
                ['~CTEN', clk('CLK'), '~LOAD', du] + rev(D(4)), ['~RCO', 'MAX/MIN'] + rev(D(4, 'Q')),
                {'kind': 'updown1', 'modulus': 10},
                ['~CTEN', 'CLK', '~LOAD', 'D/~U', 'D3', 'D2', 'D1', 'D0', '~RCO', 'MAX/MIN', 'Q3', 'Q2', 'Q1', 'Q0'], -90, -30),
        cm_chip('74165', '8-bit parallel-in serial-out shift register',
                rev(D(8)), ['SER', {'name': '~LD', 'label': 'SH/LD', 'dir': 'in'}, 'INH', {'name': 'CLK', 'clock': True, 'dir': 'in'}, '', '', '', '~QH', 'QH'],
                {'kind': 'piso'},
                rev(D(8)) + ['SER', '~LD', 'INH', 'CLK', '~QH', 'QH'], -90, -120),
        cm_chip('74174', 'Hex D flip-flop with clear', [clk('CLK')] + rev(D(6)), ['~CLR'] + rev(D(6, 'Q')),
                {'kind': 'register'}, ['CLK'] + rev(D(6)) + ['~CLR'] + rev(D(6, 'Q')), -90, -90),
        cm_chip('74293', '4-bit binary ripple counter (÷2 and ÷8)', ['R01', 'R02', clk('~CKA'), clk('~CKB')],
                ['QD', 'QC', 'QB', 'QA'], {'kind': 'ripple', 'modB': 8},
                ['R01', 'R02', '~CKA', '~CKB', 'QD', 'QC', 'QB', 'QA'], -30, -30),
        cm_chip('7492', 'Divide-by-12 ripple counter (÷2 and ÷6)', ['R01', 'R02', clk('~CKA'), clk('~CKB')],
                ['QD', 'QC', 'QB', 'QA'], {'kind': 'ripple', 'modB': 6, 'seq92': True},
                ['R01', 'R02', '~CKA', '~CKB', 'QD', 'QC', 'QB', 'QA'], -30, -30),
        cm_chip('74155', 'Dual 2-to-4 line decoder/demultiplexer',
                ['1C', '~1G', '', 'B', 'A', '~2G', '~2C'],
                ['~1Y3', '~1Y2', '~1Y1', '~1Y0', '~2Y3', '~2Y2', '~2Y1', '~2Y0'], {'kind': 'decoder155'},
                ['1C', '~1G', 'B', 'A', '~2G', '~2C', '~1Y3', '~1Y2', '~1Y1', '~1Y0', '~2Y3', '~2Y2', '~2Y1', '~2Y0'], -90, -90),
        cm_chip('74257', 'Quad 2-to-1 multiplexer, 3-state',
                ['S', 'B0', 'A0', 'B1', 'A1', 'B2', 'A2', 'B3', 'A3', '~OE'], ['Y0', '', 'Y1', '', 'Y2', '', 'Y3'],
                {'kind': 'mux2'}, ['S', 'B0', 'A0', 'B1', 'A1', 'B2', 'A2', 'B3', 'A3', '~OE', 'Y0', 'Y1', 'Y2', 'Y3'], -120, -60),
        cm_chip('74280', '9-bit odd/even parity generator/checker',
                ['I6', 'I5', 'I4', 'I3', 'I2', 'I1', 'I0'], ['I7', 'I8', {'name': 'EVEN', 'dir': 'out'}, {'name': 'ODD', 'dir': 'out'}],
                {'kind': 'parity'}, ['I6', 'I5', 'I4', 'I3', 'I2', 'I1', 'I0', 'I7', 'I8', 'EVEN', 'ODD'], -90, -90),
        cm_chip('74251', '8-to-1 multiplexer, 3-state', rev(D(8)) + ['C', 'B', 'A', '~G'], ['~W', 'Y'],
                {'kind': 'mux', 'tristate': True}, rev(D(8)) + ['C', 'B', 'A', '~G', '~W', 'Y'], -150, 0),
        cm_chip('74377', 'Octal D flip-flop with enable', ['~G', clk('CLK')] + rev(D(8)), rev(D(8, 'Q')),
                {'kind': 'register'}, ['~G', 'CLK'] + rev(D(8)) + rev(D(8, 'Q')), -120, -60),
        cm_chip('7495', '4-bit parallel-access shift register',
                ['MODE', clk('~CLK1'), clk('~CLK2')] + rev(D(4)) + ['SER'], rev(D(4, 'Q')), {'kind': 'shift95'},
                ['MODE', '~CLK1', '~CLK2'] + rev(D(4)) + ['SER'] + rev(D(4, 'Q')), -90, 30),
        cm_chip('74245', 'Octal bus transceiver, 3-state',
                ['~G'] + [{'name': f'A{i}', 'dir': 'inout'} for i in range(7, -1, -1)],
                ['DIR'] + [{'name': f'B{i}', 'dir': 'inout'} for i in range(7, -1, -1)], {'kind': 'transceiver'},
                rev(D(8, 'A')) + rev(D(8, 'B')) + ['~G', 'DIR'], -120, -120),
        cm_chip('4585', '4-bit magnitude comparator (CMOS)',
                rev(D(4, 'A')) + rev(D(4, 'B')) + ['I<', 'I=', 'I>'], ['A>B', 'A=B', 'A<B'], {'kind': 'comparator'},
                rev(D(4, 'A')) + rev(D(4, 'B')) + ['I<', 'I=', 'I>', 'A<B', 'A=B', 'A>B'], -120, 0,
                aliases=['4585', 'CD4585'], floatInputs=0),
        cm_chip('RAM1K', 'Static RAM 1K x 8',
                rev(D(10, 'A')), ['~CS', '~WE'] + [{'name': f'D{i}', 'dir': 'inout'} for i in range(7, -1, -1)],
                {'kind': 'ram'}, rev(D(10, 'A')) + rev(D(8)) + ['~CS', '~WE'], -120, -120,
                aliases=['1K RAM'], title='RAM 1Kx8',
                description='Static RAM, 1024 x 8: ~WE low writes, high reads; outputs float when ~CS is high'),
        cm_chip('PROM32', 'PROM 32 x 8', ['~CS', '', '', '', ''] + rev(D(5, 'A')), [''] + rev(D(8)),
                {'kind': 'rom'}, ['~CS'] + rev(D(5, 'A')) + rev(D(8)), -120, -120,
                aliases=['PROM32'], title='PROM 32x8', props={'data': ''},
                description='Programmable ROM, 32 x 8; contents as hex bytes in the "data" property'),
    ]
    return chips


def keypad():
    rows = [-90, -30, 30, 90]
    return {'id': 'keypad4x4', 'name': '4x4 Switch', 'prefix': 'K', 'display': 'hexkey', 'aliases': ['4x4 Switch'],
            'cmPins': ['R0', 'R1', 'R2', 'R3', 'C3', 'C0', 'C1', 'C2'], 'model': {'kind': 'keymatrix'},
            'description': 'Key matrix: holding a key connects its row to its column',
            'graphics': [R(-120, -120, 240, 240)] + [L(-150, y, -120, y) for y in rows] + [L(x, 120, x, 150) for x in rows],
            'pins': [pin(f'R{i}', -150, y) for i, y in enumerate(rows)] + [pin(f'C{i}', x, 150) for i, x in enumerate(rows)]}


# ---------------------------------------------------------------- analog
# "spice": {"line": template, "models": [...], "subckt": [...]} — {name} is the
# component's unique suffix, {value} its label, {PIN} the node of pin PIN.
# Kinds that need computed values (generator, potentiometer, meter, switches,
# digital parts) are written by SpiceNetlister itself.

MODELS = {
    'D1N4148': '.model D1N4148 D(Is=2.52n Rs=.568 N=1.752 Cjo=4p M=.4 Tt=20n Bv=100 Ibv=100u)',
    'D1N4001': '.model D1N4001 D(Is=14.11n N=1.984 Rs=33.89m Ikf=94.81 Cjo=25.89p M=.44 Vj=.3245 Bv=75 Ibv=10u Tt=5.7u)',
    'DZ5V1': '.model DZ5V1 D(Is=1n Rs=5 N=1.5 Bv=5.1 Ibv=5m)',
    'DLED': '.model DLED D(Is=1e-22 N=1.5 Rs=2 Bv=5 Ibv=10u)',
    'Q2N3904': '.model Q2N3904 NPN(Is=6.734f Xti=3 Eg=1.11 Vaf=74.03 Bf=416.4 Ne=1.259 Ise=6.734f Ikf=66.78m '
               'Xtb=1.5 Br=.7371 Nc=2 Rc=1 Cjc=3.638p Mjc=.3085 Vjc=.75 Fc=.5 Cje=4.493p Mje=.2593 Vje=.75 '
               'Tr=239.5n Tf=301.2p Itf=.4 Vtf=4 Xtf=2 Rb=10)',
    'Q2N3906': '.model Q2N3906 PNP(Is=1.41f Xti=3 Eg=1.11 Vaf=18.7 Bf=180.7 Ne=1.5 Ikf=80m Xtb=1.5 Br=4.977 '
               'Nc=2 Rc=2.5 Cjc=9.728p Mjc=.5776 Vjc=.75 Fc=.5 Cje=8.063p Mje=.3677 Vje=.75 Tr=33.42n '
               'Tf=179.3p Itf=.4 Vtf=4 Xtf=6 Rb=10)',
    'NMOS_GEN': '.model NMOS_GEN NMOS(Level=1 Vto=2 Kp=0.5 Lambda=0.01)',
    'PMOS_GEN': '.model PMOS_GEN PMOS(Level=1 Vto=-2 Kp=0.5 Lambda=0.01)',
}

# Behavioural op-amp: 100 dB gain, 1 MHz GBW, 2 Mohm input, output limited to
# 1 V inside the rails.
OPAMP = ['.subckt OPAMP_GEN inp inn vp vn out',
         'Rin inp inn 2Meg',
         'Gm 0 mid inp inn 1m',
         'Rm mid 0 100Meg',
         'Cm mid 0 159p',
         'Bo outi 0 V = min(max(v(mid), v(vn)+1), v(vp)-1)',
         'Ro outi out 50',
         'Rs vp vn 100k',
         '.ends OPAMP_GEN']


def sp(line, *models, subckt=None):
    d = {'line': line}
    if models: d['models'] = [MODELS[m] for m in models]
    if subckt: d['subckt'] = subckt
    return d


def analog():
    A = 'Analog'
    two_lead = lambda x: [L(-x, 0, -x + 30, 0), L(x - 30, 0, x, 0)]
    return [
        {'id': 'capacitor', 'name': 'Capacitor', 'prefix': 'C', 'label': '1uF', 'aliases': ['Capacitor'],
         'cmPins': ['1', '2'], 'spice': sp('C{name} {1} {2} {value}'),
         'graphics': [L(-30, 0, -5, 0), L(5, 0, 30, 0), L(-5, -15, -5, 15), L(5, -15, 5, 15)],
         'pins': [pin('1', -30, 0), pin('2', 30, 0)]},
        {'id': 'polarcap', 'name': 'Polar Cap', 'prefix': 'C', 'label': '10uF', 'aliases': ['Polar Cap'],
         'cmPins': ['+', '-'], 'spice': sp('C{name} {+} {-} {value}'),
         'graphics': [L(-40, 0, -15, 0), L(-15, -15, -15, 15), P('M -2 -15 Q -8 0 -2 15'), L(-5, 0, 20, 0),
                      T('+', -25, -12, 8)],
         'pins': [pin('+', -40, 0), pin('-', 20, 0)]},
        {'id': 'inductor', 'name': 'Inductor', 'prefix': 'L', 'label': '1mH', 'aliases': ['Inductor', 'Coil'],
         'cmPins': ['1', '2'], 'spice': sp('L{name} {1} {2} {value}'),
         'graphics': [L(-60, 0, -40, 0), L(40, 0, 60, 0),
                      P('M -40 0 Q -40 -14 -30 -14 Q -20 -14 -20 0 Q -20 -14 -10 -14 Q 0 -14 0 0 '
                        'Q 0 -14 10 -14 Q 20 -14 20 0 Q 20 -14 30 -14 Q 40 -14 40 0')],
         'pins': [pin('1', -60, 0), pin('2', 60, 0)]},
        {'id': 'diode', 'name': 'Diode', 'prefix': 'D', 'label': '1N4148', 'aliases': ['Diode'],
         'cmPins': ['A', 'K'], 'spice': sp('D{name} {A} {K} D1N4148', 'D1N4148'),
         'graphics': [L(-30, 0, -10, 0), P('M -10 -12 L 10 0 L -10 12 Z'), L(10, -12, 10, 12), L(10, 0, 30, 0)],
         'pins': [pin('A', -30, 0), pin('K', 30, 0)]},
        {'id': 'rectifier', 'name': 'Rectifier Diode', 'prefix': 'D', 'label': '1N4001', 'aliases': [],
         'spice': sp('D{name} {A} {K} D1N4001', 'D1N4001'),
         'graphics': [L(-30, 0, -10, 0), P('M -10 -12 L 10 0 L -10 12 Z'), L(10, -12, 10, 12), L(10, 0, 30, 0)],
         'pins': [pin('A', -30, 0), pin('K', 30, 0)]},
        {'id': 'zener', 'name': 'Zener Diode', 'prefix': 'D', 'label': '5V1', 'aliases': ['Zener Diode'],
         'cmPins': ['A', 'K'], 'spice': sp('D{name} {A} {K} DZ5V1', 'DZ5V1'),
         'description': '5.1 V zener',
         'graphics': [L(-30, 0, -10, 0), P('M -10 -12 L 10 0 L -10 12 Z'), P('M 15 -15 L 10 -12 L 10 12 L 5 15'),
                      L(10, 0, 30, 0)],
         'pins': [pin('A', -30, 0), pin('K', 30, 0)]},
        {'id': 'npn', 'name': 'NPN Transistor', 'prefix': 'Q', 'label': '2N3904', 'aliases': ['NPN Trans:C', 'NPN Trans'],
         'cmPins': ['C', 'B', 'E'], 'spice': sp('Q{name} {C} {B} {E} Q2N3904', 'Q2N3904'),
         'graphics': [C(0, 0, 30), L(-60, 0, -10, 0), L(-10, -20, -10, 20), L(-10, -8, 20, -35), L(20, -35, 20, -60),
                      L(-10, 8, 20, 35), L(20, 35, 20, 60), P('M 9 32 L 20 35 L 15 25 Z')],
         'pins': [pin('C', 20, -60), pin('B', -60, 0), pin('E', 20, 60)]},
        {'id': 'pnp', 'name': 'PNP Transistor', 'prefix': 'Q', 'label': '2N3906', 'aliases': ['PNP Trans:C', 'PNP Trans'],
         'cmPins': ['C', 'B', 'E'], 'spice': sp('Q{name} {C} {B} {E} Q2N3906', 'Q2N3906'),
         'graphics': [C(0, 0, 30), L(-60, 0, -10, 0), L(-10, -20, -10, 20), L(-10, -8, 20, -35), L(20, -35, 20, -60),
                      L(-10, 8, 20, 35), L(20, 35, 20, 60), P('M -10 8 L 1 11 L -4 1 Z')],
         'pins': [pin('C', 20, -60), pin('B', -60, 0), pin('E', 20, 60)]},
        {'id': 'nmos', 'name': 'N-MOSFET', 'prefix': 'M', 'label': 'NMOS', 'aliases': ['N-EMOS 3T:A', 'N-MOSFET 3T'],
         'cmPins': ['D', 'G', 'S'], 'spice': sp('M{name} {D} {G} {S} {S} NMOS_GEN', 'NMOS_GEN'),
         'description': 'Enhancement N-channel MOSFET, Vth = 2 V',
         'graphics': [L(-60, 30, -20, 30), L(-20, -25, -20, 30), L(-10, -30, -10, -12), L(-10, -8, -10, 8),
                      L(-10, 12, -10, 30), L(-10, -20, 20, -20), L(20, -20, 20, -60), L(-10, 20, 20, 20), L(20, 20, 20, 60),
                      L(-10, 0, 20, 0), L(20, 0, 20, 20), P('M -10 0 L -2 -5 L -2 5 Z')],
         'pins': [pin('D', 20, -60), pin('G', -60, 30, 'in'), pin('S', 20, 60)]},
        {'id': 'pmos', 'name': 'P-MOSFET', 'prefix': 'M', 'label': 'PMOS', 'aliases': ['P-EMOS 3T:A', 'P-MOSFET 3T'],
         'cmPins': ['D', 'G', 'S'], 'spice': sp('M{name} {D} {G} {S} {S} PMOS_GEN', 'PMOS_GEN'),
         'description': 'Enhancement P-channel MOSFET, Vth = -2 V',
         'graphics': [L(-60, 30, -20, 30), L(-20, -25, -20, 30), L(-10, -30, -10, -12), L(-10, -8, -10, 8),
                      L(-10, 12, -10, 30), L(-10, -20, 20, -20), L(20, -20, 20, -60), L(-10, 20, 20, 20), L(20, 20, 20, 60),
                      L(-10, 0, 20, 0), L(20, 0, 20, 20), P('M 8 0 L 0 -5 L 0 5 Z')],
         'pins': [pin('D', 20, -60), pin('G', -60, 30, 'in'), pin('S', 20, 60)]},
        {'id': 'opamp', 'name': 'Op-Amp', 'prefix': 'U', 'label': 'OPAMP', 'aliases': ['Op-Amp5', 'Op-Amp7', 'Op-Amp'],
         'cmPins': ['+', '-', 'V+', 'V-', 'OUT'], 'spice': sp('X{name} {+} {-} {V+} {V-} {OUT} OPAMP_GEN', subckt=OPAMP),
         'description': 'Behavioural op-amp: 100 dB gain, 1 MHz GBW, output within 1 V of the rails',
         'graphics': [P('M -40 -50 L 40 0 L -40 50 Z'), L(-60, 20, -40, 20), L(-60, -20, -40, -20),
                      L(0, -40, 0, -25), L(0, 25, 0, 40), L(40, 0, 60, 0), T('+', -32, 20, 10), T('-', -32, -20, 10)],
         'pins': [pin('+', -60, 20, 'in'), pin('-', -60, -20, 'in'), pin('V+', 0, -40), pin('V-', 0, 40),
                  pin('OUT', 60, 0, 'out')]},
        {'id': 'vsource', 'name': 'V Source', 'prefix': 'V', 'label': '5V', 'aliases': ['V Source'],
         'cmPins': ['+', '-'], 'spice': sp('V{name} {+} {-} DC {value}'),
         'graphics': [C(0, 0, 25), L(0, -70, 0, -25), L(0, 25, 0, 70), T('+', 0, -12, 10), T('-', 0, 12, 10)],
         'pins': [pin('+', 0, -70), pin('-', 0, 70)]},
        {'id': 'battery', 'name': 'Battery', 'prefix': 'V', 'label': '10V', 'aliases': ['Battery'],
         'cmPins': ['+', '-'], 'spice': sp('V{name} {+} {-} DC {value}'),
         'graphics': [L(0, -40, 0, -8), L(-20, -8, 20, -8), L(-10, 0, 10, 0), L(-20, 8, 20, 8), L(-10, 16, 10, 16),
                      L(0, 16, 0, 40), T('+', 18, -18, 9)],
         'pins': [pin('+', 0, -40), pin('-', 0, 40)]},
        {'id': 'isource', 'name': 'I Source', 'prefix': 'I', 'label': '1mA', 'aliases': ['I Source'],
         'cmPins': ['+', '-'], 'spice': sp('I{name} {+} {-} DC {value}'),
         'description': 'Current flows out of the - terminal into the circuit',
         'graphics': [C(0, 0, 25), L(0, -70, 0, -25), L(0, 25, 0, 70), L(0, -14, 0, 14), P('M -6 6 L 0 14 L 6 6')],
         'pins': [pin('+', 0, -70), pin('-', 0, 70)]},
        {'id': 'siggen', 'name': 'Signal Gen', 'prefix': 'V', 'label': '1kHz', 'aliases': ['Signal Gen'],
         'cmPins': ['+', '-'], 'model': {'kind': 'siggen'},
         'props': {'wave': 'sine', 'amplitude': '1', 'frequency': '1k', 'offset': '0'},
         'description': 'Sine, square or triangle source (wave = sine/square/triangle); also the AC source',
         'graphics': [R(-40, -40, 80, 80), P('M -28 0 Q -21 -22 -14 0 Q -7 22 0 0 Q 7 -22 14 0 Q 21 22 28 0'),
                      L(40, -20, 100, -20), L(40, 20, 100, 20), T('+', 50, -28, 8), T('-', 50, 28, 8)],
         'pins': [pin('+', 100, -20), pin('-', 100, 20)]},
        {'id': 'pot', 'name': 'Potentiometer', 'prefix': 'R', 'label': '10k', 'aliases': ['Var Resistor'],
         'cmPins': ['1', 'W', '2'], 'model': {'kind': 'pot'}, 'props': {'position': 50},
         'description': 'Wiper position in percent from pin 1',
         'graphics': [L(-60, 0, -30, 0), P('M -30 0 L -25 -10 L -15 10 L -5 -10 L 5 10 L 15 -10 L 25 10 L 30 0'),
                      L(30, 0, 60, 0), L(0, -40, 0, -14), P('M -5 -22 L 0 -12 L 5 -22')],
         'pins': [pin('1', -60, 0), pin('W', 0, -40), pin('2', 60, 0)]},
        {'id': 'lamp', 'name': 'Lamp', 'prefix': 'L', 'label': '100', 'aliases': ['Lamp'], 'display': 'bulb',
         'cmPins': ['1', '2'], 'spice': sp('R{name} {1} {2} {value}'),
         'description': 'Resistive lamp; glows with the voltage across it',
         'graphics': [C(0, 0, 25), L(-18, -18, 18, 18), L(-18, 18, 18, -18), L(-40, 40, -18, 18), L(18, 18, 40, 40)],
         'pins': [pin('1', -40, 40), pin('2', 40, 40)]},
        {'id': 'ic', 'name': '.IC Initial Condition', 'prefix': 'IC', 'label': '0V', 'aliases': ['.IC'], 'cmPins': ['1'],
         'spice': sp('.ic v({1})={value}'),
         'description': 'Starts a transient analysis with this node at the given voltage',
         'graphics': [R(-25, -40, 50, 24), T('.IC', 0, -28, 9), L(0, -16, 0, 10)], 'pins': [pin('1', 0, 10)]},
        {'id': 'nodeset', 'name': '.NODESET', 'prefix': 'NS', 'label': '0V', 'aliases': ['.NODESET'], 'cmPins': ['1'],
         'spice': sp('.nodeset v({1})={value}'),
         'description': 'Initial guess for this node when finding the operating point',
         'graphics': [R(-40, -40, 80, 24), T('.NODESET', 0, -28, 8), L(0, -16, 0, 10)], 'pins': [pin('1', 0, 10)]},
        {'id': 'multimeter', 'name': 'Multimeter', 'prefix': 'M', 'label': 'V', 'aliases': ['Multimeter'],
         'display': 'meter', 'cmPins': ['+', '', '', '-'], 'model': {'kind': 'meter'}, 'props': {'mode': 'V'},
         'description': 'Voltmeter (mode V) or ammeter (mode A) between + and -',
         'graphics': [R(-80, -50, 160, 100), L(-80, 50, -80, 80), L(80, 50, 80, 80), T('+', -70, 40, 9), T('-', 70, 40, 9)],
         'pins': [pin('+', -80, 80), pin('-', 80, 80)]},
    ]


def inputs():
    return [
        {'id': 'logicswitch', 'name': 'Logic Switch', 'prefix': 'S', 'display': 'logicswitch',
         'aliases': ['Logic Switch'], 'cmPins': ['Y'], 'model': {'kind': 'logicswitch'}, 'props': {'state': 0},
         'description': 'Click during simulation to toggle 0/1',
         'graphics': [L(-20, -11, -4, -11), C(-10, 10, 4), L(-20, 10, -14, 10), L(20, 0, 40, 0),
                      T('1', -24, -11, 7, 'right'), T('0', -24, 10, 7, 'right')],
         'pins': [pin('Y', 40, 0, 'out')]},
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
         'cmPins': ['1', '2'], 'model': {'kind': 'link', 'type': 'no'}, 'props': {'travel': 7},
         'description': 'Normally open; closed while the mouse button is held',
         'graphics': [L(-30, 10, -15, 10), L(15, 10, 30, 10), C(-12, 10, 3), C(12, 10, 3),
                      dict(L(-15, 2, 15, 2), actuator=True), dict(L(0, 2, 0, -8), actuator=True),
                      dict(L(-6, -8, 6, -8), actuator=True)],
         'pins': [pin('2', -30, 10), pin('1', 30, 10)]},
        {'id': 'pb_nc', 'name': 'NC PushButton', 'prefix': 'S', 'display': 'button', 'aliases': ['NC PushButton'],
         'cmPins': ['1', '2'], 'model': {'kind': 'link', 'type': 'nc'}, 'props': {'travel': 7},
         'description': 'Normally closed; open while the mouse button is held',
         'graphics': [L(-30, 0, -15, 0), L(15, 0, 30, 0), C(-12, 0, 3), C(12, 0, 3),
                      dict(L(-15, 3, 15, 3), actuator=True), dict(L(0, 3, 0, -10), actuator=True),
                      dict(L(-6, -10, 6, -10), actuator=True)],
         'pins': [pin('2', -30, 0), pin('1', 30, 0)]},
        {'id': 'pb_spdt', 'name': 'SPDT PB', 'prefix': 'S', 'display': 'button', 'aliases': ['SPDT PB'],
         'cmPins': ['C', 'NC', 'NO'], 'model': {'kind': 'link', 'type': 'spdt'},
         'description': 'Change-over push button: C-NC at rest, C-NO while held',
         'graphics': [C(15, 0, 3), L(18, 0, 40, 0), C(-15, 0, 3), L(-30, 0, -18, 0), C(-15, 20, 3), L(-30, 20, -18, 20),
                      T('NC', -15, -9, 6), T('NO', -15, 29, 6)],
         'pins': [pin('C', 40, 0), pin('NC', -30, 0), pin('NO', -30, 20)]},
        {'id': 'spst', 'name': 'SPST Switch', 'prefix': 'S', 'display': 'spst', 'aliases': ['SPST Switch'],
         'cmPins': ['1', '2'], 'model': {'kind': 'link', 'type': 'toggle'}, 'props': {'closed': 0},
         'description': 'Click during simulation to open/close',
         'graphics': [L(-30, 0, -15, 0), L(15, 0, 30, 0), C(-12, 0, 3), C(12, 0, 3)],
         'pins': [pin('2', -30, 0), pin('1', 30, 0)]},
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
        {'id': 'scope', 'name': 'SCOPE', 'prefix': 'T', 'label': '', 'aliases': ['SCOPE'], 'cmPins': ['1'],
         'model': {'kind': 'scope'},
         'description': 'Adds this node to the waveform window (digital and analog); the label names the trace',
         'graphics': [R(-12, -34, 24, 20), P('M -8 -20 L -4 -20 L -4 -28 L 2 -28 L 2 -20 L 8 -20'), L(0, -14, 0, 0)],
         'pins': [pin('1', 0, 0, 'in')]},
        {'id': 'probe', 'name': 'Logic Probe', 'prefix': 'P', 'display': 'lamp', 'aliases': ['Logic Probe'],
         'cmPins': ['D0'], 'model': {'kind': 'display', 'bits': 1},
         'graphics': [P('M -15 -40 L 15 -40 L 15 -10 L 0 10 L -15 -10 Z'), L(0, 10, 0, 30)],
         'pins': [pin('D0', 0, 30, 'in')]},
        {'id': 'hexdisplay', 'name': 'Hex Display', 'prefix': 'D', 'display': 'hex', 'aliases': ['Hex Display'],
         'cmPins': ['D0', 'D1', 'D2', 'D3'], 'model': {'kind': 'display', 'bits': 4},
         'description': 'Shows the hexadecimal value of D3..D0',
         'graphics': [R(-50, -70, 100, 120)] + [L(x, 50, x, 80) for x in (30, 10, -10, -30)]
                     + [T(n, x, 40, 8) for n, x in (('8', -30), ('4', -10), ('2', 10), ('1', 30))],
         'pins': [pin(f'D{i}', 30 - 20 * i, 80, 'in') for i in range(4)]},
        {'id': 'asciidisplay', 'name': 'Ascii Display', 'prefix': 'D', 'display': 'ascii', 'aliases': ['Ascii Display'],
         'cmPins': [f'D{i}' for i in range(8)], 'model': {'kind': 'display', 'bits': 7},
         'description': 'Shows the character whose 7-bit code is on D6..D0',
         'graphics': [R(-80, -120, 160, 250)] + [L(-110, -100 + 30 * i, -80, -100 + 30 * i) for i in range(8)]
                     + [T(f'D{i}', -96, -106 + 30 * i, 6) for i in range(8)],
         'pins': [pin(f'D{i}', -110, -100 + 30 * i, 'in') for i in range(8)]},
        {'id': 'led', 'name': 'LED', 'prefix': 'D', 'display': 'led', 'aliases': ['LED'], 'cmPins': ['A', 'K'],
         'model': {'kind': 'led'}, 'spice': sp('D{name} {A} {K} DLED', 'DLED'),
         'props': {'color': 'red'}, 'choices': {'color': ['red', 'green', 'yellow', 'blue', 'orange', 'white']},
         'description': 'Lights when the anode is high and the cathode low; colour in the properties',
         'graphics': [L(0, -30, 0, -10), P('M -12 -10 L 12 -10 L 0 10 Z'), L(-12, 10, 12, 10), L(0, 10, 0, 30),
                      L(14, -6, 26, -18), P('M 21 -18 L 26 -18 L 26 -13'),
                      L(18, 2, 30, -10), P('M 25 -10 L 30 -10 L 30 -5')],
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
        {'id': 'vcc', 'name': '+V', 'prefix': 'V', 'label': '5V', 'aliases': ['+V', '+5V', 'Vcc'], 'cmPins': ['VCC'],
         'model': {'kind': 'const', 'value': 1}, 'spice': sp('V{name} {VCC} 0 DC {value}'),
         'graphics': [L(0, 30, 0, 10), C(0, 0, 10), L(-5, 0, 5, 0), L(0, -5, 0, 5)], 'pins': [pin('VCC', 0, 30, 'out')]},
        {'id': 'terminal', 'name': 'Terminal', 'prefix': 'T', 'netLabel': True,
         'aliases': ['Terminal', 'Input', 'Output'], 'cmPins': ['1'],
         'description': 'Connection point; terminals with the same label are connected',
         'graphics': [L(0, 30, 0, 8), C(0, 0, 8)], 'pins': [pin('1', 0, 30)]},
        {'id': 'netlabel', 'name': 'Net label', 'prefix': '', 'netLabel': True, 'hidden': True,
         'description': 'Joins its pin to every other net label with the same text',
         'graphics': [], 'pins': [pin('1', 0, 0)]},
        {'id': 'unknown', 'name': 'Unknown device', 'prefix': 'X', 'hidden': True,
         'description': 'Placeholder for an imported device that has no equivalent in the library',
         'graphics': [R(-40, -30, 80, 60), T('?', 0, 0, 24)], 'pins': []},
        {'id': 'resistor', 'name': 'Resistor', 'prefix': 'R', 'label': '1k', 'aliases': ['Resistor', 'ResistorWire'],
         'cmPins': ['1', '2'], 'model': {'kind': 'link', 'type': 'resistor'}, 'spice': sp('R{name} {1} {2} {value}'),
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
    write('40_inputs.json', 'Digital/Inputs', inputs() + [keypad()])
    write('50_outputs.json', 'Digital/Outputs', outputs())
    write('60_ttl_gates.json', 'Digital/74xx Gates', package_gates(TTL_GATES, ttl=True))
    write('70_ttl_msi.json', 'Digital/74xx MSI', msi() + cm_chips())
    write('80_cmos_gates.json', 'Digital/40xx CMOS', package_gates(CMOS_GATES, ttl=False))
    write('90_analog.json', 'Analog', analog())
    print('library written to', os.path.normpath(OUT))
