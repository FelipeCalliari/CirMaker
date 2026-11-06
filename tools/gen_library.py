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


def box(id, name, left=(), right=(), top=(), bottom=(), width=80, pitch=40, title=None, title_y=None, **extra):
    shape = {'type': 'box', 'width': width, 'pitch': pitch, 'left': list(left), 'right': list(right),
             'top': list(top), 'bottom': list(bottom)}
    if title: shape['title'] = title
    if title_y is not None: shape['titleY'] = title_y
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
CM_GATE_ALIASES = {'nor2': ['2-In NOR:DM'], 'nand2': ['2-In NAND:DM'], 'and2': ['2-Input AND'], 'or2': ['2-Input OR']}


def generic_gates():
    out = []
    for id, (op, n, inv, name) in GATE_KINDS.items():
        out.append(gate(id, name, op, n, inv, [name] + CM_GATE_ALIASES.get(id, []), parts=parts(GENERIC_PACKAGE[id]),
                        package=GENERIC_PACKAGE[id]))
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
    ('4093', 'nand2', 4),
]
COUNT = {1: 'Single', 2: 'Dual', 3: 'Triple', 4: 'Quad', 6: 'Hex'}

# DIP pins of each gate of a package, in symbol pin order (inputs, then Y),
# in CircuitMaker's part order (A, B, ...) where its circuits record it.
_Q2_TTL = [[1, 2, 3], [4, 5, 6], [9, 10, 8], [12, 13, 11]]
_Q2_CMOS = [[1, 2, 3], [5, 6, 4], [8, 9, 10], [12, 13, 11]]
_HEX = [[1, 2], [3, 4], [5, 6], [9, 8], [11, 10], [13, 12]]
_T3_TTL = [[1, 2, 13, 12], [3, 4, 5, 6], [9, 10, 11, 8]]
_T3_CMOS = [[3, 4, 5, 6], [1, 2, 8, 9], [11, 12, 13, 10]]
_D4_TTL = [[1, 2, 4, 5, 6], [9, 10, 12, 13, 8]]
_D4_CMOS = [[2, 3, 4, 5, 1], [9, 10, 11, 12, 13]]
PACKAGE_PARTS = {
    '7400': _Q2_TTL, '7408': _Q2_TTL, '7432': _Q2_TTL, '7486': _Q2_TTL, '74266': _Q2_CMOS,
    '7402': [[3, 2, 1], [5, 6, 4], [8, 9, 10], [11, 12, 13]],
    '7404': _HEX, '7407': _HEX, '4069': _HEX,
    '4049': [[3, 2], [5, 4], [7, 6], [9, 10], [11, 12], [14, 15]], '4050': [[3, 2], [5, 4], [7, 6], [9, 10], [11, 12], [14, 15]],
    '7410': _T3_TTL, '7411': _T3_TTL, '7415': _T3_TTL, '7427': _T3_TTL,
    '7420': _D4_TTL, '7421': _D4_TTL, '7422': _D4_TTL,
    '7430': [[1, 2, 3, 4, 5, 6, 11, 12, 8]],
    '4001': _Q2_CMOS, '4011': _Q2_CMOS, '4030': _Q2_CMOS, '4070': _Q2_CMOS, '4071': _Q2_CMOS, '4077': _Q2_CMOS,
    '4081': _Q2_CMOS, '4093': _Q2_CMOS,
    '4023': _T3_CMOS, '4025': _T3_CMOS, '4073': _T3_CMOS, '4075': _T3_CMOS,
    '4002': _D4_CMOS, '4012': _D4_CMOS, '4072': _D4_CMOS, '4082': _D4_CMOS,
    '4068': [[2, 3, 4, 5, 9, 10, 11, 12, 13]], '4078': [[2, 3, 4, 5, 9, 10, 11, 12, 13]],
}
# the package CircuitMaker puts generic gates in
GENERIC_PACKAGE = {'inv': '7404', 'buf': '7407', 'and2': '7408', 'and3': '7411', 'and4': '7421', 'and8': '4068',
                   'nand2': '7400', 'nand3': '7410', 'nand4': '7420', 'nand8': '7430', 'or2': '7432', 'or3': '4075',
                   'or4': '4072', 'or8': '4078', 'nor2': '7402', 'nor3': '4025', 'nor4': '4002', 'nor8': '4078',
                   'xor2': '7486', 'xnor2': '74266'}


def parts(number):
    return [[str(n) for n in gate] for gate in PACKAGE_PARTS[number]]


def package_gates(table, ttl):
    out = []
    for number, kind, count in table:
        op, n, inv, gname = GATE_KINDS[kind]
        aliases = [number] + ([f'74LS{number[2:]}', f'74HC{number[2:]}'] if ttl else [f'CD{number}'])
        name = f'{number} {COUNT[count]} {gname}'
        extra = {'label': number, 'parts': parts(number)}
        if ttl:
            extra['floatInputs'] = 1
        if number == '7422':
            # CircuitMaker's 7422 has its pin 3 above pin 2
            extra['cmPins'] = ['A', 'C', 'B', 'D', 'Y']
        g = gate(number, name, op, n, inv, aliases,
                 f'{COUNT[count]} {gname} ({"TTL" if ttl else "CMOS"}); one gate per symbol, grouped by package', **extra)
        if number == '7422':
            g['shape']['pitch'] = 40            # and its inputs 12 units apart
        out.append(g)
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
                  model={'kind': 'dff'}, aliases=['7474'], label='7474', floatInputs=1,
                  cmPins=['~PRE', 'D', 'CLK', '~CLR', '~Q', 'Q'],
                  description='Positive-edge D flip-flop, active-low preset/clear (74x74, one of two)'))
    ff.append(box('jkff', 'JK Flip-Flop', ['J', clk('CLK'), 'K'], ['Q', Qn], model={'kind': 'jkff'},
                  aliases=['JK Flip-Flop'], cmPins=['J', 'CLK', 'K', '~Q', 'Q'], description='Positive-edge JK flip-flop'))
    ff.append(box('jkff_sr', '4027 JK Flip-Flop', ['J', clk('CLK'), 'K'], ['Q', Qn], ['S'], ['R'],
                  model={'kind': 'jkff'}, aliases=['4027', 'CD4027'], label='4027',
                  cmPins=['S', 'J', 'CLK', 'K', 'R', '~Q', 'Q'],
                  description='Positive-edge JK flip-flop, active-high set/reset (CD4027, one of two)'))
    ff.append(box('jkff_pcn', '74112 JK Flip-Flop', ['J', clk('~CLK'), 'K'], ['Q', Qn], ['~PRE'], ['~CLR'],
                  model={'kind': 'jkff'}, aliases=['74112', '7476'], label='74112', floatInputs=1,
                  cmPins=['~PRE', 'J', '~CLK', 'K', '~CLR', '~Q', 'Q'],
                  description='Negative-edge JK flip-flop, active-low preset/clear (74x112, one of two)'))
    ff.append(box('jkff_rn', '7473 JK Flip-Flop', ['J', clk('~CLK'), 'K'], ['Q', Qn], [], ['~CLR'],
                  model={'kind': 'jkff'}, aliases=['JK RN', '7473', '74107'], label='7473', floatInputs=1,
                  cmPins=['J', '~CLK', 'K', '~CLR', '~Q', 'Q'],
                  description='Negative-edge JK flip-flop, active-low clear (74x73, one of two)'))
    return ff

# DIP package pin numbers (TI datasheets; dual parts: the first one). The
# .ckt importer matches CircuitMaker pins through them.
def _pins(spec):
    return {name: int(num) for name, num in (p.rsplit('=', 1) for p in spec.split())}


MSI_PINOUTS = {
    '7490': _pins('CKB=1 R01=2 R02=3 R91=6 R92=7 QC=8 QB=9 QD=11 QA=12 CKA=14'),
    '7493': _pins('CKB=1 R01=2 R02=3 QC=8 QB=9 QD=11 QA=12 CKA=14'),
    '74161': _pins('CLR=1 CLK=2 D0=3 D1=4 D2=5 D3=6 ENP=7 LOAD=9 ENT=10 Q3=11 Q2=12 Q1=13 Q0=14 RCO=15'),
    '74192': _pins('D1=1 Q1=2 Q0=3 DOWN=4 UP=5 Q2=6 Q3=7 D3=9 D2=10 LOAD=11 CO=12 BO=13 CLR=14 D0=15'),
    '74138': _pins('A=1 B=2 C=3 G2A=4 G2B=5 G1=6 Y7=7 Y6=9 Y5=10 Y4=11 Y3=12 Y2=13 Y1=14 Y0=15'),
    '74139': _pins('1G=1 1A=2 1B=3 1Y0=4 1Y1=5 1Y2=6 1Y3=7 2Y3=9 2Y2=10 2Y1=11 2Y0=12 2B=13 2A=14 2G=15'),
    '7442': _pins('Y0=1 Y1=2 Y2=3 Y3=4 Y4=5 Y5=6 Y6=7 Y7=9 Y8=10 Y9=11 D=12 C=13 B=14 A=15'),
    '7447': _pins('B=1 C=2 LT=3 BI=4 RBI=5 D=6 A=7 e=9 d=10 c=11 b=12 a=13 g=14 f=15'),
    '74151': _pins('D3=1 D2=2 D1=3 D0=4 Y=5 W=6 G=7 C=9 B=10 A=11 D7=12 D6=13 D5=14 D4=15'),
    '74153': _pins('1G=1 B=2 1D3=3 1D2=4 1D1=5 1D0=6 1Y=7 2Y=9 2D0=10 2D1=11 2D2=12 2D3=13 A=14 2G=15'),
    '74157': _pins('S=1 A0=2 B0=3 Y0=4 A1=5 B1=6 Y1=7 Y2=9 B2=10 A2=11 Y3=12 B3=13 A3=14 G=15'),
    '7483': _pins('A3=1 S2=2 A2=3 B2=4 S1=6 B1=7 A1=8 S0=9 A0=10 B0=11 CI=13 CO=14 S3=15 B3=16'),
    '74283': _pins('S1=1 B1=2 A1=3 S0=4 A0=5 B0=6 CI=7 CO=9 S3=10 B3=11 A3=12 S2=13 A2=14 B2=15'),
    '7485': {**_pins('B3=1 A=B=6 B0=9 A0=10 B1=11 A1=12 A2=13 B2=14 A3=15'),
             'I<': 2, 'I=': 3, 'I>': 4, 'A>B': 5, 'A<B': 7},
    '74175': _pins('CLR=1 Q0=2 ~Q0=3 D0=4 D1=5 ~Q1=6 Q1=7 CLK=9 Q2=10 ~Q2=11 D2=12 D3=13 ~Q3=14 Q3=15'),
    '74273': _pins('CLR=1 Q0=2 D0=3 D1=4 Q1=5 Q2=6 D2=7 D3=8 Q3=9 CLK=11 Q4=12 D4=13 D5=14 Q5=15 Q6=16 D6=17 D7=18 Q7=19'),
    '74374': _pins('OE=1 Q0=2 D0=3 D1=4 Q1=5 Q2=6 D2=7 D3=8 Q3=9 CLK=11 Q4=12 D4=13 D5=14 Q5=15 Q6=16 D6=17 D7=18 Q7=19'),
    '74164': _pins('A=1 B=2 Q0=3 Q1=4 Q2=5 Q3=6 CLK=8 CLR=9 Q4=10 Q5=11 Q6=12 Q7=13'),
    '74194': _pins('CLR=1 SR=2 D0=3 D1=4 D2=5 D3=6 SL=7 S0=9 S1=10 CLK=11 Q3=12 Q2=13 Q1=14 Q0=15'),
    '74244': _pins('1G=1 A0=2 Y7=3 A1=4 Y6=5 A2=6 Y5=7 A3=8 Y4=9 A4=11 Y3=12 A5=13 Y2=14 A6=15 Y1=16 A7=17 Y0=18 2G=19'),
}
MSI_PINOUTS['74163'] = MSI_PINOUTS['74161']
MSI_PINOUTS['74193'] = MSI_PINOUTS['74192']
MSI_PINOUTS['7448'] = MSI_PINOUTS['7447']
MSI_PINOUTS['74373'] = {**MSI_PINOUTS['74374'], 'LE': 11}


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
        ('74139', 'Dual 2-to-4 line decoder', [], [], {'kind': 'dual', 'inner': 'decoder'}),
        ('7442', 'BCD-to-decimal decoder', ['A', 'B', 'C', 'D'], [f'~Y{i}' for i in range(10)], {'kind': 'decoder'}),
        ('7447', 'BCD to 7-segment decoder/driver, active-low outputs (common anode)',
         ['A', 'B', 'C', 'D', '~LT', '~BI', '~RBI'], ['~' + c for c in 'abcdefg'], {'kind': 'bcd7seg'}),
        ('7448', 'BCD to 7-segment decoder, active-high outputs (common cathode)',
         ['A', 'B', 'C', 'D', '~LT', '~BI', '~RBI'], list('abcdefg'), {'kind': 'bcd7seg'}),
        ('74151', '8-to-1 multiplexer', D(8) + ['A', 'B', 'C', '~G'], ['Y', '~W'], {'kind': 'mux'}),
        ('74153', 'Dual 4-to-1 multiplexer', [], [], {'kind': 'dual', 'inner': 'mux'}),
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
    for number, desc, _left, _right, model in chips:
        # CircuitMaker's own pin order and symbol layout, so imported wires
        # land on the right pins
        layout = CM_MSI_LAYOUT[number]
        pinout = MSI_PINOUTS[number]
        sides = {}
        for name, side, y in layout:
            o = {'name': name, 'number': str(pinout[name.lstrip('~')])}
            if side == 'r' and name in CM_RIGHT_INPUTS:
                o['dir'] = 'in'
            if name.lstrip('~').startswith(('CK', 'CLK', 'UP', 'DOWN')) and model['kind'] != 'register' or name == 'CLK':
                o['clock'] = True
            sides.setdefault(side, {})[y] = o

        def column(side):
            ys = sorted(sides.get(side, {}))
            if not ys:
                return [], 0
            # CircuitMaker grid 9 = pitch 30 here
            return [sides[side].get(y, '') for y in range(ys[0], ys[-1] + 1, 9)], ys[0] * 10 // 3
        left, left_start = column('l')
        right, right_start = column('r')
        out.append(cm_chip(number, desc, left, right, model, [n for n, _, _ in layout], left_start, right_start,
                           aliases=[number, f'74LS{number[2:]}', f'74HC{number[2:]}', f'74F{number[2:]}']))
    return out


# inputs drawn on the right-hand side of CircuitMaker's symbols
CM_RIGHT_INPUTS = {'~LOAD', '~CLR', '~G', 'A', 'B', 'C', '~1G', '~2G', 'I<', 'I=', 'I>', 'CLK', 'LE',
                   'D0', 'D1', 'D2', 'D3', '~BI'}

# CircuitMaker pin order of each MSI chip: (our pin, side, y in CircuitMaker
# grid units, 9 = one pin pitch), read from its symbol library.
CM_MSI_LAYOUT = {
    '7490': [('R91', 'l', -27), ('R92', 'l', -18), ('R01', 'l', -9), ('R02', 'l', 0), ('~CKA', 'l', 18), ('~CKB', 'l', 27), ('QD', 'r', -27), ('QC', 'r', -9), ('QB', 'r', 9), ('QA', 'r', 27)],
    '7493': [('R01', 'l', -9), ('R02', 'l', 0), ('~CKA', 'l', 9), ('~CKB', 'l', 18), ('QD', 'r', -9), ('QC', 'r', 0), ('QB', 'r', 9), ('QA', 'r', 18)],
    '74161': [('ENP', 'l', -36), ('ENT', 'l', -27), ('CLK', 'l', -18), ('D3', 'l', 9), ('D2', 'l', 18), ('D1', 'l', 27), ('D0', 'l', 36), ('~LOAD', 'r', -36), ('~CLR', 'r', -27), ('RCO', 'r', 0), ('Q3', 'r', 9), ('Q2', 'r', 18), ('Q1', 'r', 27), ('Q0', 'r', 36)],
    '74163': [('ENP', 'l', -27), ('ENT', 'l', -18), ('CLK', 'l', -9), ('~LOAD', 'l', 0), ('D3', 'l', 9), ('D2', 'l', 18), ('D1', 'l', 27), ('D0', 'l', 36), ('~CLR', 'r', -27), ('RCO', 'r', 0), ('Q3', 'r', 9), ('Q2', 'r', 18), ('Q1', 'r', 27), ('Q0', 'r', 36)],
    '74192': [('UP', 'l', -27), ('DOWN', 'l', -18), ('~LOAD', 'l', -9), ('CLR', 'l', 0), ('D3', 'l', 9), ('D2', 'l', 18), ('D1', 'l', 27), ('D0', 'l', 36), ('~CO', 'r', -9), ('~BO', 'r', 0), ('Q3', 'r', 9), ('Q2', 'r', 18), ('Q1', 'r', 27), ('Q0', 'r', 36)],
    '74193': [('UP', 'l', -27), ('DOWN', 'l', -18), ('~LOAD', 'l', -9), ('CLR', 'l', 0), ('D3', 'l', 9), ('D2', 'l', 18), ('D1', 'l', 27), ('D0', 'l', 36), ('~CO', 'r', -9), ('~BO', 'r', 0), ('Q3', 'r', 9), ('Q2', 'r', 18), ('Q1', 'r', 27), ('Q0', 'r', 36)],
    '74138': [('C', 'l', -27), ('B', 'l', -18), ('A', 'l', -9), ('G1', 'l', 18), ('~G2B', 'l', 27), ('~G2A', 'l', 36), ('~Y7', 'r', -27), ('~Y6', 'r', -18), ('~Y5', 'r', -9), ('~Y4', 'r', 0), ('~Y3', 'r', 9), ('~Y2', 'r', 18), ('~Y1', 'r', 27), ('~Y0', 'r', 36)],
    '74139': [('1B', 'l', -18), ('1A', 'l', -9), ('~1G', 'l', 0), ('2B', 'l', 18), ('2A', 'l', 27), ('~2G', 'l', 36), ('~1Y3', 'r', -27), ('~1Y2', 'r', -18), ('~1Y1', 'r', -9), ('~1Y0', 'r', 0), ('~2Y3', 'r', 9), ('~2Y2', 'r', 18), ('~2Y1', 'r', 27), ('~2Y0', 'r', 36)],
    '7442': [('D', 'l', 18), ('C', 'l', 27), ('B', 'l', 36), ('A', 'l', 45), ('~Y9', 'r', -36), ('~Y8', 'r', -27), ('~Y7', 'r', -18), ('~Y6', 'r', -9), ('~Y5', 'r', 0), ('~Y4', 'r', 9), ('~Y3', 'r', 18), ('~Y2', 'r', 27), ('~Y1', 'r', 36), ('~Y0', 'r', 45)],
    '7447': [('D', 'l', -36), ('C', 'l', -27), ('B', 'l', -18), ('A', 'l', -9), ('~LT', 'l', 36), ('~RBI', 'l', 45), ('~g', 'r', -36), ('~f', 'r', -27), ('~e', 'r', -18), ('~d', 'r', -9), ('~c', 'r', 0), ('~b', 'r', 9), ('~a', 'r', 18), ('~BI', 'r', 45)],
    '7448': [('D', 'l', -36), ('C', 'l', -27), ('B', 'l', -18), ('A', 'l', -9), ('~LT', 'l', 36), ('~RBI', 'l', 45), ('g', 'r', -36), ('f', 'r', -27), ('e', 'r', -18), ('d', 'r', -9), ('c', 'r', 0), ('b', 'r', 9), ('a', 'r', 18), ('~BI', 'r', 45)],
    '74151': [('D7', 'l', -27), ('D6', 'l', -18), ('D5', 'l', -9), ('D4', 'l', 0), ('D3', 'l', 9), ('D2', 'l', 18), ('D1', 'l', 27), ('D0', 'l', 36), ('~G', 'r', -27), ('C', 'r', -18), ('B', 'r', -9), ('A', 'r', 0), ('Y', 'r', 27), ('~W', 'r', 36)],
    '74153': [('1D3', 'l', -36), ('1D2', 'l', -27), ('1D1', 'l', -18), ('1D0', 'l', -9), ('B', 'l', 0), ('A', 'l', 9), ('2D3', 'l', 18), ('2D2', 'l', 27), ('2D1', 'l', 36), ('2D0', 'l', 45), ('~1G', 'r', -36), ('~2G', 'r', 45), ('1Y', 'r', -18), ('2Y', 'r', 27)],
    '74157': [('S', 'l', -36), ('B0', 'l', -27), ('A0', 'l', -18), ('B1', 'l', -9), ('A1', 'l', 0), ('B2', 'l', 9), ('A2', 'l', 18), ('B3', 'l', 27), ('A3', 'l', 36), ('~G', 'l', 45), ('Y0', 'r', -18), ('Y1', 'r', 0), ('Y2', 'r', 18), ('Y3', 'r', 36)],
    '7483': [('A3', 'l', -36), ('A2', 'l', -27), ('A1', 'l', -18), ('A0', 'l', -9), ('B3', 'l', 0), ('B2', 'l', 9), ('B1', 'l', 18), ('B0', 'l', 27), ('CI', 'l', 45), ('S3', 'r', -9), ('S2', 'r', 0), ('S1', 'r', 9), ('S0', 'r', 18), ('CO', 'r', 45)],
    '7485': [('A3', 'l', -27), ('A2', 'l', -18), ('A1', 'l', -9), ('A0', 'l', 0), ('B3', 'l', 9), ('B2', 'l', 18), ('B1', 'l', 27), ('B0', 'l', 36), ('I<', 'r', -27), ('I=', 'r', -18), ('I>', 'r', -9), ('A<B', 'r', 18), ('A=B', 'r', 27), ('A>B', 'r', 36)],
    '74175': [('~CLR', 'l', -27), ('CLK', 'l', -18), ('D3', 'l', 0), ('D2', 'l', 9), ('D1', 'l', 18), ('D0', 'l', 27), ('Q3', 'r', -27), ('~Q3', 'r', -18), ('Q2', 'r', -9), ('~Q2', 'r', 0), ('Q1', 'r', 9), ('~Q1', 'r', 18), ('Q0', 'r', 27), ('~Q0', 'r', 36)],
    '74273': [('~CLR', 'l', -36), ('CLK', 'l', -27), ('D7', 'l', -18), ('D6', 'l', -9), ('D5', 'l', 0), ('D4', 'l', 9), ('D3', 'l', 18), ('D2', 'l', 27), ('D1', 'l', 36), ('D0', 'l', 45), ('Q7', 'r', -18), ('Q6', 'r', -9), ('Q5', 'r', 0), ('Q4', 'r', 9), ('Q3', 'r', 18), ('Q2', 'r', 27), ('Q1', 'r', 36), ('Q0', 'r', 45)],
    '74374': [('D7', 'l', -27), ('D6', 'l', -18), ('D5', 'l', -9), ('D4', 'l', 0), ('D3', 'l', 9), ('D2', 'l', 18), ('D1', 'l', 27), ('D0', 'l', 36), ('Q7', 'r', -27), ('Q6', 'r', -18), ('Q5', 'r', -9), ('Q4', 'r', 0), ('Q3', 'r', 9), ('Q2', 'r', 18), ('Q1', 'r', 27), ('Q0', 'r', 36), ('~OE', 'l', -36), ('CLK', 'r', -36)],
    '74373': [('D7', 'l', -27), ('D6', 'l', -18), ('D5', 'l', -9), ('D4', 'l', 0), ('D3', 'l', 9), ('D2', 'l', 18), ('D1', 'l', 27), ('D0', 'l', 36), ('Q7', 'r', -27), ('Q6', 'r', -18), ('Q5', 'r', -9), ('Q4', 'r', 0), ('Q3', 'r', 9), ('Q2', 'r', 18), ('Q1', 'r', 27), ('Q0', 'r', 36), ('~OE', 'l', -36), ('LE', 'r', -36)],
    '74164': [('A', 'l', -27), ('B', 'l', -18), ('CLK', 'l', 0), ('~CLR', 'l', 18), ('Q7', 'r', -27), ('Q6', 'r', -18), ('Q5', 'r', -9), ('Q4', 'r', 0), ('Q3', 'r', 9), ('Q2', 'r', 18), ('Q1', 'r', 27), ('Q0', 'r', 36)],
    '74194': [('CLK', 'l', -36), ('S1', 'l', -18), ('S0', 'l', -9), ('SR', 'l', 9), ('SL', 'l', 18), ('~CLR', 'l', 36), ('D3', 'r', -36), ('D2', 'r', -27), ('D1', 'r', -18), ('D0', 'r', -9), ('Q3', 'r', 9), ('Q2', 'r', 18), ('Q1', 'r', 27), ('Q0', 'r', 36)],
    '74244': [('A3', 'l', -27), ('A2', 'l', -18), ('A1', 'l', -9), ('A0', 'l', 0), ('A4', 'l', 18), ('A5', 'l', 27), ('A6', 'l', 36), ('A7', 'l', 45), ('Y3', 'r', -27), ('Y2', 'r', -18), ('Y1', 'r', -9), ('Y0', 'r', 0), ('Y4', 'r', 18), ('Y5', 'r', 27), ('Y6', 'r', 36), ('Y7', 'r', 45), ('~1G', 'l', -36), ('~2G', 'l', 9)],
    '74283': [('A3', 'l', -36), ('A2', 'l', -27), ('A1', 'l', -18), ('A0', 'l', -9), ('B3', 'l', 0), ('B2', 'l', 9), ('B1', 'l', 18), ('B0', 'l', 27), ('CI', 'l', 45), ('S3', 'r', -9), ('S2', 'r', 0), ('S1', 'r', 9), ('S0', 'r', 18), ('CO', 'r', 45)],
}




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
                ['1C', '~1G', '', 'B', 'A', '', '~2G', '~2C'],
                ['~1Y3', '~1Y2', '~1Y1', '~1Y0', '~2Y3', '~2Y2', '~2Y1', '~2Y0'], {'kind': 'decoder155'},
                ['1C', '~1G', 'B', 'A', '~2G', '~2C', '~1Y3', '~1Y2', '~1Y1', '~1Y0', '~2Y3', '~2Y2', '~2Y1', '~2Y0'], -90, -90),
        cm_chip('74257', 'Quad 2-to-1 multiplexer, 3-state',
                ['S', 'B0', 'A0', 'B1', 'A1', 'B2', 'A2', 'B3', 'A3', '~OE'], ['Y0', '', 'Y1', '', 'Y2', '', 'Y3'],
                {'kind': 'mux2'}, ['S', 'B0', 'A0', 'B1', 'A1', 'B2', 'A2', 'B3', 'A3', '~OE', 'Y0', 'Y1', 'Y2', 'Y3'], -120, -60),
        cm_chip('74280', '9-bit odd/even parity generator/checker',
                ['I6', 'I5', 'I4', 'I3', 'I2', 'I1', 'I0'], ['I7', 'I8', '', {'name': 'ODD', 'dir': 'out'}, {'name': 'EVEN', 'dir': 'out'}],
                {'kind': 'parity'}, ['I6', 'I5', 'I4', 'I3', 'I2', 'I1', 'I0', 'I7', 'I8', 'EVEN', 'ODD'], -90, -90),
        cm_chip('74251', '8-to-1 multiplexer, 3-state', rev(D(8)),
                [{'name': n, 'dir': 'in'} for n in ('C', 'B', 'A', '~G')] + ['', '', '~W', 'Y'],
                {'kind': 'mux', 'tristate': True}, rev(D(8)) + ['C', 'B', 'A', '~G', '~W', 'Y'], -90, -90),
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
                rev(D(4, 'A')) + rev(D(4, 'B')) + ['', 'I>', 'I=', 'I<'], ['A>B', 'A=B', 'A<B'], {'kind': 'comparator'},
                rev(D(4, 'A')) + rev(D(4, 'B')) + ['I>', 'I=', 'I<', 'A<B', 'A=B', 'A>B'], -120, 0,
                aliases=['4585', 'CD4585'], floatInputs=0),
        cm_chip('4017', 'Decade counter/divider with 10 decoded outputs (CMOS)',
                ['~CP1', clk('CP0'), '', 'MR'], ['~Q5-9'] + [f'Q{i}' for i in range(9, -1, -1)], {'kind': 'johnson'},
                ['~CP1', 'CP0', 'MR'] + [f'Q{i}' for i in range(10)] + ['~Q5-9'], 60, -120,
                aliases=['4017', 'CD4017'], floatInputs=0),
        cm_chip('74169', 'Synchronous up/down 4-bit binary counter',
                ['~CEP', '~CET', clk('CP'), '', ''] + rev(D(4)),
                [{'name': '~PE', 'dir': 'in'}, {'name': 'U/~D', 'label': 'U/D', 'dir': 'in'}, '', '', '~TC'] + rev(D(4, 'Q')),
                {'kind': 'updown169', 'modulus': 16},
                ['~CEP', '~CET', 'CP'] + rev(D(4)) + ['~PE', 'U/~D', '~TC'] + rev(D(4, 'Q')), -120, -120),
        cm_chip('74168', 'Synchronous up/down decade counter',
                ['~CEP', '~CET', clk('CP'), '', ''] + rev(D(4)),
                [{'name': '~PE', 'dir': 'in'}, {'name': 'U/~D', 'label': 'U/D', 'dir': 'in'}, '', '', '~TC'] + rev(D(4, 'Q')),
                {'kind': 'updown169', 'modulus': 10},
                ['~CEP', '~CET', 'CP'] + rev(D(4)) + ['~PE', 'U/~D', '~TC'] + rev(D(4, 'Q')), -120, -120),
        cm_chip('74173', '4-bit D register, 3-state outputs',
                ['MR', '~E1', '~E2', clk('CP')] + rev(D(4)),
                [{'name': '~OE1', 'dir': 'in'}, {'name': '~OE2', 'dir': 'in'}, '', ''] + rev(D(4, 'Q')),
                {'kind': 'reg173'}, ['MR', '~E1', '~E2', 'CP'] + rev(D(4)) + ['~OE1', '~OE2'] + rev(D(4, 'Q')), -90, -90),
        cm_chip('7475', '4-bit bistable latch',
                ['D3', 'D2', 'E23', '', 'D1', 'D0', 'E01'], ['Q3', '~Q3', 'Q2', '~Q2', 'Q1', '~Q1', 'Q0', '~Q0'],
                {'kind': 'latch75'}, ['D3', 'D2', 'E23', 'D1', 'D0', 'E01', 'Q3', '~Q3', 'Q2', '~Q2', 'Q1', '~Q1', 'Q0', '~Q0'],
                -90, -90),
        cm_chip('74181', '4-bit arithmetic logic unit',
                ['S3', 'S2', 'S1', 'S0'] + [f'~A{i}' for i in range(3, -1, -1)] + [f'~B{i}' for i in range(3, -1, -1)],
                [{'name': 'Cn', 'dir': 'in'}, {'name': 'M', 'dir': 'in'}, '', '', 'Cn+4', 'A=B', '~G', '~P']
                + [f'~F{i}' for i in range(3, -1, -1)],
                {'kind': 'alu181'},
                ['S3', 'S2', 'S1', 'S0'] + [f'~A{i}' for i in range(3, -1, -1)] + [f'~B{i}' for i in range(3, -1, -1)]
                + ['Cn', 'M', 'Cn+4', 'A=B', '~G', '~P'] + [f'~F{i}' for i in range(3, -1, -1)], -150, -150),
        {'id': 'quad_dff', 'name': 'Quad D Flop', 'prefix': 'U', 'label': '', 'aliases': ['Quad D Flop'],
         'cmPins': ['D0', 'D1', 'D2', 'D3', 'Q0', 'Q1', 'Q2', 'Q3', 'CLK'], 'model': {'kind': 'register'},
         'description': 'Four D flip-flops on one rising clock',
         'graphics': [R(-50, -120, 100, 210)] + [L(-80, y, -50, y) for y in (-100, -60, -20, 20)]
                     + [L(50, y, 80, y) for y in (-100, -60, -20, 20)] + [L(0, 90, 0, 120), P('M -8 90 L 0 80 L 8 90')]
                     + [T(f'D{i}', -42, y, 8, 'left') for i, y in enumerate((-100, -60, -20, 20))]
                     + [T(f'Q{i}', 42, y, 8, 'right') for i, y in enumerate((-100, -60, -20, 20))],
         'pins': [pin(f'D{i}', -80, y, 'in') for i, y in enumerate((-100, -60, -20, 20))]
                 + [pin(f'Q{i}', 80, y, 'out') for i, y in enumerate((-100, -60, -20, 20))]
                 + [pin('CLK', 0, 120, 'in', clock=True)]},
        cm_chip('RAM1K', 'Static RAM 1K x 8',
                rev(D(10, 'A')), ['~CS', '~WE'] + [{'name': f'D{i}', 'dir': 'inout'} for i in range(7, -1, -1)],
                {'kind': 'ram'}, rev(D(10, 'A')) + rev(D(8)) + ['~CS', '~WE'], -120, -120,
                aliases=['1K RAM'], title='RAM 1Kx8', props={'data': ''},
                description='Static RAM, 1024 x 8: ~WE low writes, high reads; outputs float when ~CS is high; '
                            'initial contents as hex bytes in the "data" property'),
        cm_chip('PROM32', 'PROM 32 x 8', ['~CS', '', '', ''] + rev(D(5, 'A')), [''] + rev(D(8)),
                {'kind': 'rom'}, ['~CS'] + rev(D(5, 'A')) + rev(D(8)), -120, -120,
                aliases=['PROM32'], title='PROM 32x8', props={'data': ''},
                description='Programmable ROM, 32 x 8; contents as hex bytes in the "data" property'),
    ]
    return chips + dual_flipflops()


def dual_flipflops():
    """Whole 74x74/73/112 packages as CircuitMaker's 74LS parts draw them."""
    ff = lambda h, names: [n.replace('#', str(h)) for n in names]
    out = []
    for number, desc, inner, left, right, starts in [
        ('74LS74', 'Dual D flip-flop, active-low preset/clear', 'dff',
         ff(1, ['#CLK', '#D', '~#PRE', '~#CLR']) + [''] + ff(2, ['#CLK', '#D', '~#PRE', '~#CLR']),
         ff(1, ['#Q', '~#Q']) + ['', '', ''] + ff(2, ['#Q', '~#Q']), (-120, -90)),
        ('74LS73', 'Dual JK flip-flop, negative edge, active-low clear', 'jkff',
         ff(1, ['#J', '#K', '~#CLK', '~#CLR']) + ff(2, ['#J', '#K', '~#CLK', '~#CLR']),
         ff(1, ['#Q', '~#Q']) + ['', ''] + ff(2, ['#Q', '~#Q']), (-90, -60)),
        ('74LS112', 'Dual JK flip-flop, negative edge, active-low preset/clear', 'jkff',
         ff(1, ['#J', '#K', '~#CLK', '~#PRE', '~#CLR']) + ff(2, ['#J', '#K', '~#CLK', '~#PRE', '~#CLR']),
         ff(1, ['#Q', '~#Q']) + ['', '', ''] + ff(2, ['#Q', '~#Q']), (-120, -120)),
    ]:
        left = [{'name': n, 'clock': True} if 'CLK' in n else n for n in left]
        names = [e['name'] if isinstance(e, dict) else e for e in left + right if e]
        out.append(cm_chip(number, desc, left, right, {'kind': 'dual', 'inner': inner}, names, *starts,
                           aliases=[number, '74HC' + number[4:]], title=number))
    return out


def keypad():
    rows = [-70, -20, 20, 70]          # CircuitMaker's +-22 and +-7 units
    return {'id': 'keypad4x4', 'name': '4x4 Switch', 'prefix': 'K', 'display': 'hexkey', 'aliases': ['4x4 Switch'],
            'cmPins': ['R0', 'R1', 'R2', 'R3', 'C3', 'C0', 'C1', 'C2'], 'model': {'kind': 'keymatrix'},
            'description': 'Key matrix: holding a key connects its row to its column',
            'graphics': [R(-100, -100, 200, 200)] + [L(-150, y, -100, y) for y in rows] + [L(x, 100, x, 150) for x in rows],
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
        {'id': 'inductor', 'name': 'Inductor', 'prefix': 'L', 'label': '1mH', 'aliases': ['Inductor', 'Coil', 'Coil 3T'],
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
         'cmPins': ['1', '2'], 'model': {'kind': 'load'}, 'spice': sp('R{name} {1} {2} {value}'),
         'description': 'Resistive lamp; glows with the voltage across it',
         'graphics': [C(0, 0, 25), L(-18, -18, 18, 18), L(-18, 18, 18, -18), L(-20, 40, -20, 15), L(20, 15, 20, 40)],
         'pins': [pin('1', -20, 40, float=-1), pin('2', 20, 40, float=-1)]},     # 12 units apart, as in CircuitMaker
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
    ] + power_parts() + misc_parts() + controlled_sources() + rare_parts()

N = '\n'   # separates the lines of a multi-element template
MODELS['J2N3819'] = '.model J2N3819 NJF(Beta=1.304m Betatce=-.5 Rd=1 Rs=1 Lambda=2.25m Vto=-3 Vtotc=-2.5m Is=33.57f Cgd=1.6p Cgs=2.414p)'
MODELS['DBRIDGE'] = '.model DBRIDGE D(Is=14.11n N=1.984 Rs=33.89m Bv=600 Ibv=10u)'
# crystal: series R-L-C at the label's frequency plus the holder capacitance
XTAL = ['.subckt XTAL_GEN 1 2 params: f=4Meg',
        'Rm 1 a 50',
        'Lm a b {1/(4*3.14159265*3.14159265*f*f*20e-15)}',
        'Cm b 2 20f',
        'C0 1 2 5p',
        '.ends XTAL_GEN']
DARLINGTON_PNP = ['.subckt DARL_PNP c b e',
                  'Q1 c b x Q2N3906',
                  'Q2 c x e Q2N3906',
                  'R1 x e 10k',
                  '.ends DARL_PNP']


def misc_parts():
    lead = lambda x0, y0, x1, y1: L(x0, y0, x1, y1)
    return [
        {'id': 'crystal', 'name': 'Crystal', 'prefix': 'Y', 'label': '4Meg', 'aliases': ['Crystal'],
         'cmPins': ['1', '2'], 'spice': sp('X{name} {1} {2} XTAL_GEN params: f={value}', subckt=XTAL),
         'description': 'Quartz crystal: series resonance at the label frequency (SPICE units: 4Meg = 4 MHz)',
         'graphics': [L(-40, 0, -12, 0), L(-12, -16, -12, 16), R(-7, -20, 14, 40), L(12, -16, 12, 16), L(12, 0, 40, 0)],
         'pins': [pin('1', -40, 0), pin('2', 40, 0)]},
        {'id': 'fuse', 'name': 'Fuse', 'prefix': 'F', 'label': '1A', 'aliases': ['Fuse'],
         'cmPins': ['1', '2'], 'spice': sp('R{name} {1} {2} 10m'),
         'description': 'Fuse (10 milliohm; it does not blow in the simulation)',
         'graphics': [L(-70, 0, -40, 0), R(-40, -10, 80, 20), P('M -30 0 Q -15 -12 0 0 Q 15 12 30 0'), L(40, 0, 70, 0)],
         'pins': [pin('1', -70, 0), pin('2', 70, 0)]},
        {'id': 'fw_bridge', 'name': 'FW Bridge', 'prefix': 'D', 'label': 'BRIDGE', 'aliases': ['FW Bridge'],
         'cmPins': ['AC1', '+', 'AC2', '-'],
         'spice': sp('D{name}a {AC1} {+} DBRIDGE' + N + 'D{name}b {AC2} {+} DBRIDGE' + N + 'D{name}c {-} {AC1} DBRIDGE' + N
                     + 'D{name}d {-} {AC2} DBRIDGE', 'DBRIDGE'),
         'description': 'Full-wave bridge rectifier: AC on AC1/AC2, DC on + and -',
         'graphics': [P('M -60 -10 L 0 -70 L 60 -10 L 0 50 Z'), P('M -36 -46 L -24 -34 L -38 -32 Z'),
                      P('M 24 -46 L 36 -34 L 38 -48 Z'), P('M -36 26 L -24 14 L -22 28 Z'), P('M 24 14 L 36 26 L 22 28 Z'),
                      L(-100, -10, -60, -10), L(60, -10, 110, -10), L(0, -120, 0, -70), L(0, 50, 0, 90),
                      T('+', 10, -100, 9), T('-', 10, 80, 9)],
         'pins': [pin('AC1', -100, -10), pin('+', 0, -120), pin('AC2', 110, -10), pin('-', 0, 90)]},
        {'id': 'njfet', 'name': 'N-JFET', 'prefix': 'J', 'label': '2N3819', 'aliases': ['N-JFET'],
         'cmPins': ['D', 'G', 'S'], 'spice': sp('J{name} {D} {G} {S} J2N3819', 'J2N3819'),
         'graphics': [C(0, 0, 30), L(-40, 0, -6, 0), P('M -14 -6 L -6 0 L -14 6'), L(-6, -24, -6, 24),
                      L(-6, -16, 30, -16), L(30, -16, 30, -60), L(-6, 16, 30, 16), L(30, 16, 30, 60)],
         'pins': [pin('D', 30, -60), pin('G', -40, 0, 'in'), pin('S', 30, 60)]},
        {'id': 'pnp_darlington', 'name': 'PNP Darlington', 'prefix': 'Q', 'label': 'DARL', 'aliases': ['PNP Darling1'],
         'cmPins': ['C', 'B', 'E'], 'spice': sp('X{name} {C} {B} {E} DARL_PNP', 'Q2N3906', subckt=DARLINGTON_PNP),
         'description': 'PNP Darlington pair (two 2N3906)',
         'graphics': [C(0, 0, 32), L(-80, 0, -10, 0), L(-10, -20, -10, 20), L(-10, -8, 30, -40), L(30, -40, 30, -70),
                      L(-10, 8, 30, 40), L(30, 40, 30, 70), P('M -10 -8 L 1 -11 L -4 -1 Z'), T('2', 14, 0, 8)],
         'pins': [pin('C', 30, 70), pin('B', -80, 0), pin('E', 30, -70)]},
        {'id': 'regulator3', 'name': 'Voltage Regulator', 'prefix': 'U', 'label': '7805', 'aliases': ['Regulator3'],
         'cmPins': ['IN', 'GND', 'OUT'], 'props': {'volts': '5'},
         'spice': sp('B{name} {OUT} {GND} V=max(min(v({IN},{GND})-2, {prop:volts}), 0)' + N + 'R{name} {IN} {GND} 100k'),
         'description': 'Three-terminal regulator: OUT = the "volts" property while IN is 2 V above it',
         'graphics': [R(-60, -60, 120, 80), T('REG', 0, -30, 10), T('IN', -40, -30, 7), T('OUT', 40, -30, 7),
                      L(-90, -30, -60, -30), L(60, -30, 90, -30), L(0, 20, 0, 80)],
         'pins': [pin('IN', -90, -30), pin('GND', 0, 80), pin('OUT', 90, -30)]},
        {'id': 'buzzer', 'name': 'Piezo Buzzer', 'prefix': 'LS', 'label': '1k', 'aliases': ['Piezo Buzzer'], 'display': 'buzzer',
         'cmPins': ['1', '2'], 'model': {'kind': 'load'}, 'spice': sp('R{name} {1} {2} {value}'),
         'description': 'Piezo buzzer, simulated as a resistive load',
         'graphics': [C(0, 0, 30), C(0, 0, 18), L(-80, 0, -30, 0), L(30, 0, 80, 0)],
         'pins': [pin('1', -80, 0, float=-1), pin('2', 80, 0, float=-1)]},
        {'id': 'speaker', 'name': 'Speaker', 'prefix': 'LS', 'label': '8', 'aliases': ['Speaker'],
         'cmPins': ['1', '2'], 'model': {'kind': 'load'}, 'spice': sp('R{name} {1} {2} {value}'),
         'description': 'Loudspeaker, simulated as its resistance',
         'graphics': [R(-10, -20, 20, 40), P('M 10 -20 L 40 -45 L 40 45 L 10 20'), L(-30, -50, 0, -50), L(0, -50, 0, -20),
                      L(-30, 50, 0, 50), L(0, 50, 0, 20)],
         'pins': [pin('1', -30, -50, float=-1), pin('2', -30, 50, float=-1)]},
        {'id': 'dc_motor', 'name': 'DC Motor', 'prefix': 'M', 'label': '10', 'aliases': ['DC Motor'], 'display': 'motor',
         'cmPins': ['1', '2'], 'model': {'kind': 'load'}, 'spice': sp('R{name} {1} {name}_m {value}' + N + 'L{name} {name}_m {2} 1m'),
         'description': 'DC motor, simulated as its winding (resistance from the label, 1 mH)',
         'graphics': [C(0, 0, 30), T('M', 0, 0, 14), L(-60, 0, -30, 0), L(30, 0, 60, 0)],
         'pins': [pin('1', -60, 0, float=-1), pin('2', 60, 0, float=-1)]},
        {'id': 'solenoid', 'name': 'Solenoid', 'prefix': 'L', 'label': '5VCOIL', 'aliases': ['Solenoid'],
         'cmPins': ['1', '2'], 'model': {'kind': 'load'}, 'props': dict(COIL_PROPS), 'spice': COIL_SPICE,
         'description': COIL_DESCRIPTION,
         'graphics': [L(-50, 0, -40, 0), P('M -40 0 Q -30 -16 -20 0 Q -10 -16 0 0 Q 10 -16 20 0 Q 30 -16 40 0'),
                      L(40, 0, 50, 0), R(-40, 8, 80, 8)],
         'pins': [pin('1', -50, 0, float=-1), pin('2', 50, 0, float=-1)]},     # 28 units apart in CircuitMaker
        {'id': 'triode', 'name': 'Triode', 'prefix': 'V', 'label': '12AX7', 'aliases': ['Triode'],
         'cmPins': ['P', 'G', 'K'],
         'spice': sp('B{name} {P} {K} I=1.15e-6*pow(max(100*v({G},{K})+v({P},{K}), 0), 1.5)' + N
                     + 'R{name}g {G} {K} 1G' + N + 'C{name} {G} {K} 2p'),
         'description': 'Vacuum triode (3/2-power law, mu = 100, like a 12AX7 section)',
         'graphics': [C(0, 0, 45), L(0, -90, 0, -25), L(-20, -25, 20, -25), L(-90, 0, -30, 0),
                      P('M -24 0 L -16 0 M -8 0 L 0 0 M 8 0 L 16 0'), P('M -20 25 L 20 25 L 20 30'),
                      L(-40, 80, -40, 30), L(-40, 30, -20, 25)],
         'pins': [pin('P', 0, -90), pin('G', -90, 0, 'in'), pin('K', -40, 80)]},
        {'id': 'tetrode', 'name': 'Tetrode', 'prefix': 'V', 'label': 'TETRODE', 'aliases': ['Tetrode'],
         'cmPins': ['P', 'S', 'G', 'K'],
         'spice': sp('B{name} {P} {K} I=1e-6*pow(max(20*v({G},{K})+v({S},{K}), 0), 1.5)*v({P},{K})/(abs(v({P},{K}))+20)'
                     + N + 'B{name}s {S} {K} I=0.2e-6*pow(max(20*v({G},{K})+v({S},{K}), 0), 1.5)' + N
                     + 'R{name}g {G} {K} 1G' + N + 'R{name}p {P} {K} 10Meg'),
         'description': 'Vacuum tetrode (screen grid S); simplified 3/2-power law',
         'graphics': [C(0, 0, 45), L(0, -90, 0, -25), L(-20, -25, 20, -25), L(-90, 10, -30, 10),
                      P('M -24 10 L -16 10 M -8 10 L 0 10 M 8 10 L 16 10'), P('M -24 -8 L -16 -8 M -8 -8 L 0 -8 M 8 -8 L 16 -8'),
                      L(20, -8, 90, -8), P('M -20 25 L 20 25 L 20 30'), L(-40, 80, -40, 30), L(-40, 30, -20, 25)],
         'pins': [pin('P', 0, -90), pin('S', 90, -10), pin('G', -90, 10, 'in'), pin('K', -40, 80)]},
    ]


MODELS['VSW_GEN'] = '.model VSW_GEN sw vt=2.5 vh=0.1 ron=1 roff=1G'
MODELS['ISW_GEN'] = '.model ISW_GEN csw it=1m ih=0.1m ron=1 roff=1G'


def controlled_sources():
    """CircuitMaker's dependent sources: output on pins 1-2 (right), control on
    3-4 (left); current control flows from 3 to 4 through a 0 V sense source."""
    def source(id, name, cm, line, desc, label, inner, models=()):
        return {'id': id, 'name': name, 'prefix': id[0].upper() if id[0] in 'eghf' else 'S', 'label': label,
                'aliases': [cm], 'cmPins': ['1', '2', '3', '4'], 'spice': sp(line, *models), 'description': desc,
                'graphics': [L(-70, -90, -30, -90), L(-30, -90, -30, -20), L(-70, 90, -30, 90), L(-30, 90, -30, 20),
                             T('+', -44, -60, 9), T('-', -44, 60, 9),
                             L(70, -90, 30, -90), L(30, -90, 30, -40), L(70, 90, 30, 90), L(30, 90, 30, 40)] + inner,
                'pins': [pin('1', 70, -90), pin('2', 70, 90), pin('3', -70, -90, 'in'), pin('4', -70, 90, 'in')]}
    diamond = [P('M 30 -40 L 60 0 L 30 40 L 0 0 Z')]
    sense = 'V{name}s {3} {4} DC 0' + N
    return [
        source('vcvs', 'V->V Source', 'V->V Source', 'E{name} {1} {2} {3} {4} {value}',
               'Voltage-controlled voltage source: V(1,2) = gain x V(3,4)', '1', diamond + [T('+', 30, -18, 8), T('-', 30, 18, 8)]),
        source('vccs', 'V->I Source', 'V->I Source', 'G{name} {1} {2} {3} {4} {value}',
               'Voltage-controlled current source: I(1->2) = gain x V(3,4) (A/V)', '1m',
               diamond + [L(30, -20, 30, 20), P('M 25 12 L 30 20 L 35 12')]),
        source('ccvs', 'I->V Source', 'I->V Source', sense + 'H{name} {1} {2} V{name}s {value}',
               'Current-controlled voltage source: V(1,2) = gain x I(3->4) (V/A)', '1k',
               diamond + [T('+', 30, -18, 8), T('-', 30, 18, 8)]),
        source('cccs', 'I->I Source', 'I->I Source', sense + 'F{name} {1} {2} V{name}s {value}',
               'Current-controlled current source: I(1->2) = gain x I(3->4)', '1',
               diamond + [L(30, -20, 30, 20), P('M 25 12 L 30 20 L 35 12')]),
        source('vswitch', 'V->Switch', 'V->Switch', 'S{name} {1} {2} {3} {4} VSW_GEN',
               'Voltage-controlled switch: 1-2 closed (1 ohm) while V(3,4) is above 2.5 V', '2.5V',
               [L(30, -40, 30, -10), L(30, -10, 50, 25), L(30, 25, 30, 40), C(30, -10, 2), C(30, 25, 2)], ['VSW_GEN']),
        source('iswitch', 'I->Switch', 'I->Switch', sense + 'W{name} {1} {2} V{name}s ISW_GEN',
               'Current-controlled switch: 1-2 closed (1 ohm) while I(3->4) is above 1 mA', '1mA',
               [L(30, -40, 30, -10), L(30, -10, 50, 25), L(30, 25, 30, 40), C(30, -10, 2), C(30, 25, 2)], ['ISW_GEN']),
    ]


# UJT: base divider (2.4k + 3.6k, eta 0.6) whose lower half conducts more
# with emitter current -- the negative resistance of a relaxation oscillator
UJT = ['.subckt UJT_GEN e b1 b2',
       'Rb2 b2 x 2.4k',
       'Ve e e1 0',
       'D1 e1 x DUJT',
       'Bb1 x b1 I=v(x,b1)/3.6k*(1+40*tanh(max(i(Ve),0)/2m))',
       'Cx x b1 1n',
       '.model DUJT D(Is=1e-14 Rs=10 Cjo=10p)',
       '.ends UJT_GEN']
# PUT: an SCR fired from the anode side; the gate sets the peak voltage
PUT = ['.subckt PUT_GEN a g k',
       'Qp x g a PPUT',
       'Qn g x k NPUT',
       'Rx x k 10k',
       '.model PPUT PNP(Bf=20 Is=1e-14 Cje=5p Cjc=5p)',
       '.model NPUT NPN(Bf=100 Is=1e-14 Cje=5p Cjc=5p)',
       '.ends PUT_GEN']


def _bit(i): return f'(floor(c/{2 ** i})-2*floor(c/{2 ** (i + 1)}))'


# ADC: code = (Vin - Vref-)/(Vref+ - Vref-) x 256, clamped; outputs 0/5 V
_ADC_CODE = 'min(max(floor((v({Vin})-v({VREF-}))/max(v({VREF+})-v({VREF-}),1e-9)*256),0),255)'
ADC_LINES = ['R{name}in {Vin} {VREF-} 1Meg', 'B{name}eoc {EOC} 0 V=5'] + [
    f'B{{name}}{i} {{D{i}}} 0 V=5*{_bit(i).replace("c", _ADC_CODE)}' for i in range(8)]


def rare_parts():
    D = lambda n, p='D': [f'{p}{i}' for i in range(n)]
    adc_lines = ADC_LINES
    dac_code = '+'.join(f'(v({{D{i}}})>2.5?{2 ** i}:0)' for i in range(8))
    return [
        {'id': 'ujt', 'name': 'N-UJT', 'prefix': 'Q', 'label': '2N2646', 'aliases': ['N-UJT'],
         'cmPins': ['E', 'B1', 'B2'], 'spice': sp('X{name} {E} {B1} {B2} UJT_GEN', subckt=UJT),
         'description': 'Unijunction transistor (eta 0.6, Rbb 6k): fires when E rises above 0.6 V(B2,B1) + 0.6 V',
         'graphics': [C(5, 0, 30), L(-50, -30, -20, -30), L(-20, -30, 0, -10), P('M -8 -14 L 0 -10 L -4 -18'),
                      L(5, -25, 5, 25), L(5, -20, 30, -40), L(30, -40, 30, -60), L(5, 20, 30, 40), L(30, 40, 30, 60)],
         'pins': [pin('E', -50, -30, 'in'), pin('B1', 30, 60), pin('B2', 30, -60)]},
        {'id': 'put', 'name': 'PUT', 'prefix': 'Q', 'label': '2N6027', 'aliases': ['PUT'],
         'cmPins': ['A', 'G', 'K'], 'spice': sp('X{name} {A} {G} {K} PUT_GEN', subckt=PUT),
         'description': 'Programmable unijunction transistor: conducts A to K once A rises 0.6 V above G',
         'graphics': [L(50, 0, 14, 0), P('M 14 -12 L 14 12 L -14 0 Z'), L(-14, -12, -14, 12), L(-14, 0, -50, 0),
                      L(14, -12, 30, -30), L(30, -30, 50, -30)],
         'pins': [pin('A', 50, 0), pin('G', 50, -30, 'in'), pin('K', -50, 0)]},
        {'id': 'nlv', 'name': 'NLV Source', 'prefix': 'B', 'label': 'NLV', 'aliases': ['NLV Source'],
         'cmPins': ['V+', 'V-'], 'props': {'expr': '0'}, 'spice': sp('B{name} {V+} {V-} V={prop:expr}'),
         'description': 'Nonlinear voltage source: V = the "expr" property, a SPICE expression of node voltages '
                        '(v(out), v(a,b)), currents of sources (i(v1)) and time',
         'graphics': [C(0, 0, 25), L(0, -70, 0, -25), L(0, 25, 0, 70), T('NL', 0, 0, 9), T('+', 14, -34, 9)],
         'pins': [pin('V+', 0, -70), pin('V-', 0, 70)]},
        {'id': 'nli', 'name': 'NLI Source', 'prefix': 'B', 'label': 'NLI', 'aliases': ['NLI Source'],
         'cmPins': ['I+', 'I-'], 'props': {'expr': '0'}, 'spice': sp('B{name} {I+} {I-} I={prop:expr}'),
         'description': 'Nonlinear current source: I (out of I-) = the "expr" property, a SPICE expression',
         'graphics': [C(0, 0, 25), L(0, -70, 0, -25), L(0, 25, 0, 70), L(0, -14, 0, 14), P('M -6 6 L 0 14 L 6 6')],
         'pins': [pin('I+', 0, -70), pin('I-', 0, 70)]},
        {'id': 'adc8', 'name': 'ADC8', 'prefix': 'U', 'label': 'ADC8', 'aliases': ['ADC8'],
         'cmPins': ['Vin', 'VREF+', 'VREF-', 'SC', 'OE', 'EOC'] + D(8),
         'shape': {'type': 'box', 'width': 160, 'pitch': 30, 'left': ['Vin', '', 'VREF+', 'VREF-', '', 'SC', 'OE'],
                   'right': [{'name': f'D{i}', 'dir': 'out'} for i in range(7, -1, -1)] + [{'name': 'EOC', 'dir': 'out'}],
                   'leftStart': -60, 'rightStart': -90, 'title': 'ADC8'},
         'spice': sp(N.join(adc_lines)),
         'description': '8-bit analog-to-digital converter (analog simulation): D = (Vin - VREF-) / (VREF+ - VREF-) x 256, '
                        'outputs 0/5 V, converting continuously'},
        {'id': 'dac8', 'name': 'DAC8', 'prefix': 'U', 'label': 'DAC8', 'aliases': ['DAC8'],
         'cmPins': [f'D{i}' for i in range(7, -1, -1)] + ['VREF-', 'VREF+', 'Vout'],
         'shape': {'type': 'box', 'width': 160, 'pitch': 30, 'left': [f'D{i}' for i in range(7, -1, -1)],
                   'right': [{'name': 'Vout', 'dir': 'out'}, '', {'name': 'VREF+', 'dir': 'in'}, {'name': 'VREF-', 'dir': 'in'}],
                   'leftStart': -90, 'rightStart': -30, 'title': 'DAC8'},
         'spice': sp('B{name} {Vout} 0 V=v({VREF-})+(v({VREF+})-v({VREF-}))*(' + dac_code + ')/256'),
         'description': '8-bit digital-to-analog converter (analog simulation): Vout = VREF- + (VREF+ - VREF-) x D / 256, '
                        'inputs high above 2.5 V'},
        {'id': 'tline', 'name': 'Lossless Line', 'prefix': 'T', 'label': 'LLTR', 'aliases': ['LossLessLine'],
         'cmPins': ['N1+', 'N1-', 'N2+', 'N2-'], 'props': {'z0': '50', 'td': '10n'},
         'spice': sp('T{name} {N1+} {N1-} {N2+} {N2-} Z0={prop:z0} TD={prop:td}'),
         'description': 'Lossless transmission line: impedance z0, delay td',
         'graphics': [R(-130, -20, 260, 40), L(-160, -10, -130, -10), L(-160, 10, -130, 10), L(130, -10, 160, -10),
                      L(130, 10, 160, 10), T('Z0', 0, 0, 9)],
         'pins': [pin('N1+', -160, -10), pin('N1-', -160, 10), pin('N2+', 160, -10), pin('N2-', 160, 10)]},
        {'id': 'coax', 'name': 'Coax', 'prefix': 'T', 'label': 'COAX', 'aliases': ['Coax'],
         'cmPins': ['N1+', 'N1-', 'N2+', 'N2-'], 'props': {'z0': '50', 'td': '10n'},
         'spice': sp('T{name} {N1+} {N1-} {N2+} {N2-} Z0={prop:z0} TD={prop:td}'),
         'description': 'Coaxial cable as a lossless line: impedance z0, delay td; N2- is the shield',
         'graphics': [R(-130, -24, 260, 48), L(-130, -10, 130, -10), L(-160, -10, -130, -10), L(-160, 10, -130, 10),
                      L(130, -10, 160, -10), L(-20, 24, -20, 150)],
         'pins': [pin('N1+', -160, -10), pin('N1-', -160, 10), pin('N2+', 160, -10), pin('N2-', -20, 150)]},
        {'id': 'twisted_pair', 'name': 'Twisted Pair', 'prefix': 'T', 'label': 'PAIR', 'aliases': ['TwistedPair'],
         'cmPins': ['N1+', 'N1-', 'N2+', 'N2-'], 'props': {'z0': '100', 'td': '10n'},
         'spice': sp('T{name} {N1+} {N1-} {N2+} {N2-} Z0={prop:z0} TD={prop:td}'),
         'description': 'Twisted pair as a lossless line: impedance z0, delay td',
         'graphics': [P('M -130 -10 Q -110 10 -90 -10 Q -70 10 -50 -10 Q -30 10 -10 -10 Q 10 10 30 -10 Q 50 10 70 -10 '
                        'Q 90 10 110 -10 L 130 -10'),
                      P('M -130 10 Q -110 -10 -90 10 Q -70 -10 -50 10 Q -30 -10 -10 10 Q 10 -10 30 10 Q 50 -10 70 10 '
                        'Q 90 -10 110 10 L 130 10'),
                      L(-160, -10, -130, -10), L(-160, 10, -130, 10), L(130, -10, 160, -10), L(130, 10, 160, 10)],
         'pins': [pin('N1+', -160, -10), pin('N1-', -160, 10), pin('N2+', 160, -10), pin('N2-', 160, 10)]},
        {'id': 'gain_block', 'name': 'Gain', 'prefix': 'A', 'label': 'x1', 'aliases': ['gain'],
         'cmPins': ['IN', 'OUT'], 'props': {'gain': '1'},
         'spice': sp('B{name} {OUT} 0 V={prop:gain}*v({IN})' + N + 'R{name} {IN} 0 1Meg'),
         'description': 'Gain block (analog): OUT = gain x IN, referred to ground',
         'graphics': [P('M -50 -40 L 60 0 L -50 40 Z'), L(-80, 0, -50, 0), L(60, 0, 130, 0), T('K', -20, 0, 10)],
         'pins': [pin('IN', -80, 0, 'in'), pin('OUT', 130, 0, 'out')]},
        {'id': 'divider_block', 'name': 'Divider', 'prefix': 'A', 'label': '÷', 'aliases': ['divider'],
         'cmPins': ['NUM', 'DEN', 'OUT'],
         'spice': sp('B{name} {OUT} 0 V=v({NUM})/(abs(v({DEN}))<1e-9?1e-9:v({DEN}))' + N + 'R{name}a {NUM} 0 1Meg' + N
                     + 'R{name}b {DEN} 0 1Meg'),
         'description': 'Divider block (analog): OUT = NUM / DEN, referred to ground',
         'graphics': [R(-50, -40, 110, 80), T('÷', 5, 0, 16), L(-80, -30, -50, -30), L(-80, 30, -50, 30), L(60, 0, 130, 0)],
         'pins': [pin('NUM', -80, -30, 'in'), pin('DEN', -80, 30, 'in'), pin('OUT', 130, 0, 'out')]},
    ]


# CircuitMaker's math blocks: the part's label names the function, as the
# device value does in CircuitMaker ("ABSV", "ADDV"; "...R" for the versions
# with floating inputs and output).
VMATH1 = {'ABSV': 'abs({x})', 'ACOSV': 'acos({x})', 'ACOSHV': 'acosh({x})', 'ASINV': 'asin({x})',
          'ASINHV': 'asinh({x})', 'ATANV': 'atan({x})', 'ATANHV': 'atanh({x})', 'COSV': 'cos({x})',
          'COSHV': 'cosh({x})', 'EXPV': 'exp({x})', 'LNV': 'ln({x})', 'LOGV': 'log10({x})', 'SINV': 'sin({x})',
          'SINHV': 'sinh({x})', 'SQRTV': 'sqrt({x})', 'TANV': 'tan({x})', 'UNARYV': '-({x})'}
VMATH2 = {'ADDV': '({a})+({b})', 'SUBV': '({a})-({b})', 'MULTV': '({a})*({b})', 'DIVV': '({a})/({b})'}


def vmath_variants(table, out, args, loads, suffix=''):
    return {name + suffix: {'line': f'B{{name}} {out} V=' + expr.format(**args) + N
                            + N.join(f'R{{name}}{k} {a} {b} 1Meg' for k, (a, b) in enumerate(loads))}
            for name, expr in table.items()}


# Phase-locked loop (CD4046-like): input buffers at half the supply, an
# exclusive-OR phase comparator, a VCO from fc - fr to fc + fr over Vin = 0..VDD,
# and the demodulator output following Vin.
PLL = ['.subckt PLL_GEN sig comp vout vss dem vin pc vdd params: fc=10k fr=5k rs=100',
       'Rin1 sig vdd 1.5Meg',
       'Rin2 sig vss 1.5Meg',
       'Bs s 0 V=0.5+0.5*tanh(40*(v(sig,vss)/(v(vdd,vss)+1e-9)-0.5))',
       'Bc k 0 V=0.5+0.5*tanh(40*(v(comp,vss)/(v(vdd,vss)+1e-9)-0.5))',
       'Bpc p vss V=v(vdd,vss)*(v(s)+v(k)-2*v(s)*v(k))',
       'Rpc p pc {rs}',
       'Bctl ctl 0 V=v(vin,vss)/(v(vdd,vss)+1e-9)',
       'Rctl ctl 0 100Meg',
       'Avco %vd(ctl 0) %vd(osc 0) vcom',
       '.model vcom square(cntl_array=[0 1] freq_array=[{fc-fr} {fc+fr}] out_low=0 out_high=1 duty_cycle=0.5 '
       'rise_time=1e-7 fall_time=1e-7)',
       'Bo o vss V=v(osc)*v(vdd,vss)',
       'Ro o vout {rs}',
       'Bd d vss V=v(vin,vss)',
       'Rd d dem {rs}',
       '.ends']


def vco_model(kind, extra=''):
    return {'models': ['.model {name}_m ' + kind + '(cntl_array=[{prop:cntl}] freq_array=[{prop:freq}] '
                       'out_low={prop:low} out_high={prop:high}' + extra + ')']}


def math_blocks():
    box1 = [R(-60, -30, 120, 60), L(-90, 0, -60, 0), L(60, 0, 90, 0), T('f(V)', 0, 0, 9)]
    return [
        {'id': 'vmath1', 'name': 'V-Math1', 'prefix': 'A', 'label': 'ABSV', 'aliases': ['V-Math1'],
         'cmPins': ['V', 'Q'],
         'spice': {'select': 'label', 'variants': vmath_variants(VMATH1, '{Q} 0', {'x': 'v({V})'}, [('{V}', '0')])},
         'description': 'Function of a voltage, referred to ground; the label picks it: ' + ', '.join(VMATH1),
         'graphics': box1, 'pins': [pin('V', -90, 0, 'in'), pin('Q', 90, 0, 'out')]},
        {'id': 'vmath1_ref', 'name': 'V-Math1 Ref', 'prefix': 'A', 'label': 'ABSVR', 'aliases': ['V-Math1 Ref'],
         'cmPins': ['V+', 'V-', 'Q-', 'Q+'],
         'spice': {'select': 'label', 'variants': vmath_variants(VMATH1, '{Q+} {Q-}', {'x': 'v({V+},{V-})'},
                                                                [('{V+}', '{V-}')], 'R')},
         'description': 'Function of a differential voltage, floating output; the label picks it: '
                        + ', '.join(k + 'R' for k in VMATH1),
         'graphics': [R(-60, -50, 120, 70), L(-90, -30, -60, -30), L(-90, 0, -60, 0), L(60, -30, 90, -30),
                      L(60, 0, 90, 0), T('f(V)', 0, -15, 9), T('+', -50, -30, 7, 'left'), T('+', 50, -30, 7, 'right')],
         'pins': [pin('V+', -90, -30, 'in'), pin('V-', -90, 0, 'in'), pin('Q-', 90, 0, 'out'), pin('Q+', 90, -30, 'out')]},
        {'id': 'vmath2', 'name': 'V-Math2', 'prefix': 'A', 'label': 'ADDV', 'aliases': ['V-Math2'],
         'cmPins': ['A', 'B', 'Q'],
         'spice': {'select': 'label', 'variants': vmath_variants(VMATH2, '{Q} 0', {'a': 'v({A})', 'b': 'v({B})'},
                                                                [('{A}', '0'), ('{B}', '0')])},
         'description': 'Sum, difference, product or quotient of two voltages, referred to ground; the label picks it: '
                        + ', '.join(VMATH2),
         'graphics': [R(-60, -50, 120, 70), L(-90, -30, -60, -30), L(-90, 0, -60, 0), L(60, -30, 90, -30),
                      T('A', -50, -30, 7, 'left'), T('B', -50, 0, 7, 'left'), T('Q', 50, -30, 7, 'right')],
         'pins': [pin('A', -90, -30, 'in'), pin('B', -90, 0, 'in'), pin('Q', 90, -30, 'out')]},
        {'id': 'vmath2_ref', 'name': 'V-Math2 Ref', 'prefix': 'A', 'label': 'ADDVR', 'aliases': ['V-Math2 Ref'],
         'cmPins': ['A+', 'A-', 'B+', 'B-', 'Q-', 'Q+'],
         'spice': {'select': 'label', 'variants': vmath_variants(VMATH2, '{Q+} {Q-}',
                                                                {'a': 'v({A+},{A-})', 'b': 'v({B+},{B-})'},
                                                                [('{A+}', '{A-}'), ('{B+}', '{B-}')], 'R')},
         'description': 'Operation on two differential voltages, floating output; the label picks it: '
                        + ', '.join(k + 'R' for k in VMATH2),
         'graphics': [R(-60, -50, 120, 130), L(-90, -30, -60, -30), L(-90, 0, -60, 0), L(-90, 30, -60, 30),
                      L(-90, 60, -60, 60), L(60, 0, 90, 0), L(60, 30, 90, 30), T('A+', -50, -30, 7, 'left'),
                      T('A-', -50, 0, 7, 'left'), T('B+', -50, 30, 7, 'left'), T('B-', -50, 60, 7, 'left'),
                      T('Q+', 50, 0, 7, 'right'), T('Q-', 50, 30, 7, 'right')],
         'pins': [pin('A+', -90, -30, 'in'), pin('A-', -90, 0, 'in'), pin('B+', -90, 30, 'in'), pin('B-', -90, 60, 'in'),
                  pin('Q-', 90, 30, 'out'), pin('Q+', 90, 0, 'out')]},
        {'id': 'gain_diff', 'name': 'Gain (differential)', 'prefix': 'A', 'label': 'x1', 'aliases': ['gain:A'],
         'cmPins': ['IN+', 'IN-', 'OUT+', 'OUT-'], 'props': {'gain': '1'},
         'spice': sp('B{name} {OUT+} {OUT-} V={prop:gain}*v({IN+},{IN-})' + N + 'R{name} {IN+} {IN-} 1Meg'),
         'description': 'Gain block with differential input and floating output: OUT = gain x (IN+ - IN-)',
         'graphics': [P('M -30 -60 L 80 0 L -30 60 Z'), L(-60, -30, -30, -30), L(-60, 30, -30, 30),
                      L(50, -16, 80, -16), L(80, -16, 80, -30), L(80, -30, 110, -30), L(50, 16, 80, 16),
                      L(80, 16, 80, 30), L(80, 30, 110, 30), T('K', 0, 0, 10), T('+', -22, -30, 7, 'left'),
                      T('-', -22, 30, 7, 'left')],
         'pins': [pin('IN+', -60, -30, 'in'), pin('IN-', -60, 30, 'in'), pin('OUT+', 110, -30, 'out'),
                  pin('OUT-', 110, 30, 'out')]},
        {'id': 'divider_diff', 'name': 'Divider (differential)', 'prefix': 'A', 'label': '÷', 'aliases': ['divider:A'],
         'cmPins': ['NUM+', 'NUM-', 'DEN+', 'DEN-', 'OUT+', 'OUT-'],
         'spice': sp('B{name} {OUT+} {OUT-} V=v({NUM+},{NUM-})/(abs(v({DEN+},{DEN-}))<1e-9?1e-9:v({DEN+},{DEN-}))' + N
                     + 'R{name}a {NUM+} {NUM-} 1Meg' + N + 'R{name}b {DEN+} {DEN-} 1Meg'),
         'description': 'Divider with differential inputs and floating output: OUT = (NUM+ - NUM-) / (DEN+ - DEN-)',
         'graphics': [R(-30, -80, 110, 160), T('÷', 25, 0, 16), L(-60, -60, -30, -60), L(-60, -30, -30, -30),
                      L(-60, 30, -30, 30), L(-60, 60, -30, 60), L(80, -30, 110, -30), L(80, 30, 110, 30),
                      T('N', -22, -45, 7, 'left'), T('D', -22, 45, 7, 'left')],
         'pins': [pin('NUM+', -60, -60, 'in'), pin('NUM-', -60, -30, 'in'), pin('DEN+', -60, 30, 'in'),
                  pin('DEN-', -60, 60, 'in'), pin('OUT+', 110, -30, 'out'), pin('OUT-', 110, 30, 'out')]},
        {'id': 'vco', 'name': 'VCO', 'prefix': 'A', 'label': 'SINEVCO', 'aliases': ['VCO'],
         'cmPins': ['C+', 'C-', 'OUT+', 'OUT-'],
         'props': {'cntl': '0 1 2 3 4', 'freq': '0 1k 2k 3k 4k', 'low': '-1', 'high': '1', 'duty': '0.5'},
         'spice': {'line': 'A{name} %vd({C+} {C-}) %vd({OUT+} {OUT-}) {name}_m', 'select': 'label',
                   'variants': {'SINEVCO': vco_model('sine'),
                                'SQRVCO': vco_model('square', ' duty_cycle={prop:duty} rise_time=1u fall_time=1u'),
                                'TRIVCO': vco_model('triangle', ' duty_cycle={prop:duty}')}},
         'description': 'Voltage-controlled oscillator: the frequency follows the control voltage through the '
                        '"cntl" and "freq" points; the label picks the wave (SINEVCO, SQRVCO, TRIVCO), '
                        'between "low" and "high"',
         'graphics': [R(-60, -80, 60, 180), L(-90, -60, -60, -60), L(-90, 80, -60, 80), L(0, -60, 30, -60),
                      L(0, 80, 30, 80), P('M -45 10 Q -38 -10 -30 10 Q -22 30 -15 10'), T('VCO', -30, -35, 8),
                      T('+', -52, -60, 7, 'left'), T('+', -8, -60, 7, 'right')],
         'pins': [pin('C+', -90, -60, 'in'), pin('C-', -90, 80, 'in'), pin('OUT+', 30, -60, 'out'),
                  pin('OUT-', 30, 80, 'out')]},
        {'id': 'pll', 'name': 'PLL', 'prefix': 'U', 'label': 'PLL10K', 'aliases': ['PLL'],
         'cmPins': ['Sig', 'Comp', 'Vout', 'Vss', 'Dem', 'Vin', 'PC', 'Vdd'],
         'props': {'fc': '10k', 'fr': '5k', 'rs': '100'},
         'spice': sp('X{name} {Sig} {Comp} {Vout} {Vss} {Dem} {Vin} {PC} {Vdd} PLL_GEN fc={prop:fc} fr={prop:fr} '
                     'rs={prop:rs}', subckt=PLL),
         'description': 'Phase-locked loop (CD4046-like): XOR phase comparator (PC), VCO from fc - fr to fc + fr '
                        'as Vin goes from Vss to Vdd (Vout), demodulator output (Dem) following Vin',
         'graphics': [R(-140, -140, 110, 160), T('PLL', -85, -150, 8), L(-170, -120, -140, -120), L(-170, -90, -140, -90),
                      L(-170, -60, -140, -60), L(-170, 0, -140, 0), L(-30, 0, 0, 0), L(-30, -30, 0, -30),
                      L(-30, -60, 0, -60), L(-30, -120, 0, -120),
                      T('Sig', -134, -120, 7, 'left'), T('Comp', -134, -90, 7, 'left'), T('Vout', -134, -60, 7, 'left'),
                      T('Vss', -134, 0, 7, 'left'), T('Dem', -36, 0, 7, 'right'), T('Vin', -36, -30, 7, 'right'),
                      T('PC', -36, -60, 7, 'right'), T('Vdd', -36, -120, 7, 'right')],
         'pins': [pin('Sig', -170, -120, 'in'), pin('Comp', -170, -90, 'in'), pin('Vout', -170, -60, 'out'),
                  pin('Vss', -170, 0), pin('Dem', 0, 0, 'out'), pin('Vin', 0, -30, 'in'), pin('PC', 0, -60, 'out'),
                  pin('Vdd', 0, -120)]},
    ]


# 555 timer: divider, two comparators, a latch (with a small RC so it is not
# an algebraic loop), totem-pole output and the discharge switch.
TIMER555 = ['.subckt TIMER555 gnd trg out rst ctl thr dis vcc',
            'R1 vcc ctl 5k',
            'R2 ctl lo 5k',
            'R3 lo gnd 5k',
            'Bs s gnd V = v(trg,gnd) < v(lo,gnd) ? 1 : 0',
            'Br r gnd V = v(thr,gnd) > v(ctl,gnd) ? 1 : 0',
            'Bq qx gnd V = v(rst,gnd) < 0.7 ? 0 : (v(s,gnd) > 0.5 ? 1 : (v(r,gnd) > 0.5 ? 0 : (v(q,gnd) > 0.5 ? 1 : 0)))',
            'Rq qx q 1k',
            'Cq q gnd 10p',
            'Bo oi gnd V = v(q,gnd) > 0.5 ? max(v(vcc,gnd) - 1.7, 0) : 0.1',
            'Ro oi out 10',
            'Bn qn gnd V = 1 - v(q,gnd)',
            'Sd dis gnd qn gnd SW555',
            'Iq vcc gnd 3m',
            '.model SW555 sw vt=0.5 vh=0.2 ron=10 roff=100Meg',
            '.ends TIMER555']

# SCR as the classic two-transistor latch; TRIAC as two of them back to back,
# the reverse one fired by gate current leaving G.
SCR = ['.subckt SCR_GEN a g k',
       'Qn x g k NSCR',
       'Qp g x a PSCR',
       'Rgk g k 1k',
       '.model NSCR NPN(Bf=100 Is=1e-14 Cje=10p Cjc=5p)',
       '.model PSCR PNP(Bf=10 Is=1e-14 Cje=10p Cjc=5p)',
       '.ends SCR_GEN']
TRIAC = ['.subckt TRIAC_GEN mt2 g mt1',
         'Vg g gi 0',
         'Qn1 x1 gi mt1 NTRI',
         'Qp1 gi x1 mt2 PTRI',
         'R1 gi mt1 1k',
         'Qn2 x2 g2 mt2 NTRI',
         'Qp2 g2 x2 mt1 PTRI',
         'R2 g2 mt2 1k',
         'B2 mt2 g2 I = max(-i(Vg), 0)',
         '.model NTRI NPN(Bf=100 Is=1e-14 Cje=10p Cjc=5p)',
         '.model PTRI PNP(Bf=10 Is=1e-14 Cje=10p Cjc=5p)',
         '.ends TRIAC_GEN']
MODELS['RELAY_NO'] = '.model RELAY_NO sw vt=2.5 vh=0.5 ron=50m roff=1G'
MODELS['RELAY_NC'] = '.model RELAY_NC sw vt=2.5 vh=0.5 ron=1G roff=50m'

# relay coils: past the pull-in voltage they close a switch from 1 V onto
# the node their contacts read (rly_<relay name>); below pull-in minus
# drop-off it opens again
COIL_PROPS = {'relay': 'K1', 'pullin': '4', 'dropoff': '0.1', 'resistance': '500', 'inductance': '10m'}
COIL_SPICE = {'line': N.join(['L{name} {1} {name}_l {prop:inductance}', 'R{name} {name}_l {2} {prop:resistance}',
                              'B{name}v {name}_v 0 V=abs(v({1},{2}))', 'V{name}one {name}_one 0 DC 1',
                              'S{name} {name}_one rly_{prop:relay} {name}_v 0 {name}_SW',
                              'R{name}k rly_{prop:relay} 0 1k']),
              'models': ['.model {name}_SW sw vt={prop:pullin} vh={prop:dropoff}']}
COIL_DESCRIPTION = ('Relay coil (resistance and inductance in the properties): above the pull-in voltage it '
                    'operates the contacts with the same relay name')


def contacts(id, name, alias, closed_at_rest):
    on, off = ('1G', '{prop:resistance}') if closed_at_rest else ('{prop:resistance}', '1G')
    other = 'NC' if closed_at_rest else 'NO'
    return {'id': id, 'name': name, 'prefix': 'K', 'label': 'NORMAL', 'aliases': [alias], 'display': 'contact',
            'cmPins': ['C', 'NC', ''] if closed_at_rest else ['C', '', 'NO'],
            'props': {'relay': 'K1', 'resistance': '0.1'},
            'model': {'kind': 'contact', 'closed': closed_at_rest},
            'spice': {'line': N.join([f'S{{name}} {{C}} {{{other}}} rly_{{prop:relay}} 0 {{name}}_SW',
                                      'R{name}k rly_{prop:relay} 0 1G']),
                      'models': [f'.model {{name}}_SW sw vt=0.5 ron={on} roff={off}']},
            'description': ('Relay contact, closed until' if closed_at_rest else 'Relay contact, open until')
                           + ' the coil with the same relay name pulls in',
            # the slash of a closed contact is drawn with its state (closed at
            # rest, or operated by its coil while simulating)
            'graphics': [L(-40, 0, -8, 0), L(-8, -12, -8, 12), L(8, -12, 8, 12), L(8, 0, 40, 0)],
            'pins': [pin('C', 40, 0), pin(other, -40, 0)]}


def transformer_lines(secondaries):
    """Ideal transformer: each secondary (S+, S-, gain) is gain x the primary
    voltage, and its current reflects into the primary."""
    lines = ['R{name}p {P1} {name}_a {prop:rp}']
    reflect = []
    for k, (plus, minus, gain) in enumerate(secondaries):
        lines += [f'B{{name}}e{k} {{name}}_c{k} {{name}}_d{k} V=({gain})*v({{name}}_a,{{P2}})',
                  f'V{{name}}{k} {{name}}_d{k} {{{minus}}} 0',
                  f'R{{name}}s{k} {{name}}_c{k} {{{plus}}} {{prop:rs}}']
        reflect.append(f'({gain})*i(V{{name}}{k})')
    lines += ['B{name}f {name}_a {P2} I=-(' + '+'.join(reflect) + ')',
              'R{name}g {' + secondaries[0][1] + '} 0 1G']
    return N.join(lines)


# MC1496 balanced modulator, from the datasheet's equivalent circuit: a
# Gilbert cell (carrier quad over the signal pair) with two current sinks.
MC1496 = ['.subckt MC1496_GEN g1 vca vcb vsa vsb vee bias outa outb g2',
          'Qs1 ta vsa g1 QMC1496', 'Qs2 tb vsb g2 QMC1496',
          'Qc1 outa vca ta QMC1496', 'Qc2 outb vcb ta QMC1496',
          'Qc3 outb vca tb QMC1496', 'Qc4 outa vcb tb QMC1496',
          'Qi1 g1 bias e1 QMC1496', 'Qi2 g2 bias e2 QMC1496',
          'Re1 e1 vee 500', 'Re2 e2 vee 500', 'Db bias eb DMC1496', 'Rb eb vee 500',
          '.model QMC1496 NPN(BF=100 IS=1e-15 VAF=100)', '.model DMC1496 D(IS=1e-15)',
          '.ends MC1496_GEN']


def cm_devices():
    winding = lambda x0, y0, y1, dx: [P(f'M {x0} {y0} ' + ' '.join(
        f'Q {x0 + dx} {y0 + (i + .5) * (y1 - y0) / 4} {x0} {y0 + (i + 1) * (y1 - y0) / 4}' for i in range(4)))]
    return [
        {'id': 'mc1496', 'name': 'MC1496', 'prefix': 'U', 'label': 'MC1496', 'aliases': ['MC1496'], 'package': 'DIP14',
         'cmPins': ['G1', 'VC+', 'VC-', 'VS+', 'VS-', 'VEE', 'BIAS', 'OUT+', 'OUT-', 'G2'],
         'shape': {'type': 'box', 'width': 160, 'pitch': 30, 'left': ['G1', 'VC+', 'VC-', 'VS+', 'VS-'],
                   'right': ['G2', 'OUT-', 'OUT+', 'BIAS', 'VEE'], 'leftStart': -60, 'rightStart': -60, 'title': 'MC1496'},
         'spice': sp('X{name} {G1} {VC+} {VC-} {VS+} {VS-} {VEE} {BIAS} {OUT+} {OUT-} {G2} MC1496_GEN', subckt=MC1496),
         'description': 'Balanced modulator/demodulator: the carrier (VC) switches the signal (VS) between the outputs; '
                        'gain resistor between G1 and G2, bias current into BIAS, VEE the negative supply'},
        # frequency to voltage: the netlister measures the period in the
        # co-simulation (ftov_core) and outputs frequency / cycles
        {'id': 'ftov', 'name': 'F->V Source', 'prefix': 'V', 'label': 'FCVS', 'aliases': ['F->V Source'],
         'cmPins': ['NC+', 'NC-', 'N+', 'N-'], 'props': {'vil': '1', 'vih': '2', 'cycles': '1k'},
         'description': 'Frequency-controlled voltage source: N+ - N- = frequency of NC+ - NC- / cycles per volt; '
                        'the input counts a cycle when it rises above VIH after falling below VIL',
         'graphics': [R(-70, -80, 80, 180), L(-90, -60, -70, -60), L(-90, 80, -70, 80), L(10, -60, 30, -60), L(10, 80, 30, 80),
                      T('f', -45, 15, 14), T('V', -5, 15, 14), L(-35, 10, -15, 10), P('M -21 6 L -15 10 L -21 14'),
                      T('+', -60, -50, 9), T('-', -60, 70, 9), T('+', 0, -50, 9), T('-', 0, 70, 9)],
         'pins': [pin('NC+', -90, -60), pin('NC-', -90, 80), pin('N+', 30, -60), pin('N-', 30, 80)]},
        {'id': 'ftov_core', 'name': 'F->V counter', 'prefix': 'X', 'hidden': True, 'model': {'kind': 'ftov'},
         'description': 'Period meter inside F->V Source: F0..F23 = frequency of IN in hertz',
         'pins': [pin('IN', -40, 0, 'in')] + [pin(f'F{i}', 40, i * 10, 'out') for i in range(24)]},
        {'id': 'trans_ratio', 'name': 'Transformer (ratio)', 'prefix': 'T', 'label': '1TO1', 'aliases': ['Trans2'],
         'cmPins': ['P1', 'P2', 'S1', 'S2'], 'props': {'ratio': '1', 'rp': '0.1', 'rs': '0.1'},
         'spice': sp(transformer_lines([('S1', 'S2', '{prop:ratio}')])),
         'description': 'Ideal transformer: secondary voltage = ratio x primary (ratio = secondary/primary turns); '
                        '0.1 ohm windings',
         'graphics': winding(-20, -30, 30, 12) + winding(20, -40, 40, -12)
                     + [L(-4, -40, -4, 40), L(4, -40, 4, 40), L(-40, -40, -20, -40), L(-20, -40, -20, -30),
                        L(-40, 40, -20, 40), L(-20, 40, -20, 30), L(20, -60, 40, -60), L(20, -60, 20, -40),
                        L(20, 60, 40, 60), L(20, 60, 20, 40)],
         'pins': [pin('P1', -40, -40), pin('P2', -40, 40), pin('S1', 40, -60), pin('S2', 40, 60)]},
        {'id': 'trans_ct', 'name': 'CT Transformer (ratio)', 'prefix': 'T', 'label': '1TO1CT', 'aliases': ['Trans3'],
         'cmPins': ['P1', 'P2', 'S1', 'CT', 'S2'], 'props': {'ratio': '1', 'rp': '0.1', 'rs': '0.1'},
         'spice': sp(transformer_lines([('S1', 'CT', '{prop:ratio}/2'), ('CT', 'S2', '{prop:ratio}/2')])),
         'description': 'Ideal centre-tapped transformer: S1-S2 = ratio x primary, half on each side of CT',
         'graphics': winding(-20, -30, 30, 12) + winding(30, -50, 50, -12)
                     + [L(6, -50, 6, 50), L(14, -50, 14, 50), L(-40, -40, -20, -40), L(-20, -40, -20, -30),
                        L(-40, 40, -20, 40), L(-20, 40, -20, 30), L(30, -60, 50, -60), L(30, -60, 30, -50),
                        L(30, 60, 50, 60), L(30, 60, 30, 50), L(30, 0, 50, 0)],
         'pins': [pin('P1', -40, -40), pin('P2', -40, 40), pin('S1', 50, -60), pin('CT', 50, 0), pin('S2', 50, 60)]},
        {'id': 'xfmr', 'name': 'Core Transformer', 'prefix': 'T', 'label': 'DEFAULT', 'aliases': ['xfmr'],
         'cmPins': ['P1', 'P2', 'S1', 'S2'], 'props': {'prit': '1', 'sect': '1', 'rp': '0.1', 'rs': '0.1'},
         'spice': sp(transformer_lines([('S1', 'S2', '({prop:sect})/({prop:prit})')])),
         'description': 'Transformer given by its turns (prit primary, sect secondary), simulated as an ideal one',
         'graphics': winding(-30, -40, 40, 12) + winding(40, -40, 40, -12)
                     + [L(0, -50, 0, 50), L(10, -50, 10, 50), L(-60, -60, -30, -60), L(-30, -60, -30, -40),
                        L(-60, 60, -30, 60), L(-30, 60, -30, 40), L(40, 60, 80, 60), L(40, 60, 40, 40),
                        L(40, -60, 80, -60), L(40, -60, 40, -40)],
         'pins': [pin('P1', -60, -60), pin('P2', -60, 60), pin('S1', 80, 60), pin('S2', 80, -60)]},
        {'id': 'relay_coil', 'name': 'Relay Coil', 'prefix': 'K', 'label': '5VCOIL', 'aliases': ['Relay Coil:A'],
         'cmPins': ['1', '2'], 'model': {'kind': 'load'}, 'props': dict(COIL_PROPS), 'spice': COIL_SPICE, 'description': COIL_DESCRIPTION,
         'graphics': [R(-45, -20, 30, 40), L(-45, 20, -15, -20), L(-30, -40, -30, -20), L(-30, 20, -30, 40)],
         'pins': [pin('1', -30, -40, float=-1), pin('2', -30, 40, float=-1)]},
        contacts('contact_no', 'Contacts NO', 'Contacts:D', False),
        contacts('contact_nc', 'Contacts NC', 'Contacts:E', True),
        {'id': 'adc0800', 'name': 'ADC0800', 'prefix': 'U', 'label': 'ADC0800', 'aliases': ['ADC0800'],
         'package': 'DIP18',
         'cmPins': ['D4', 'D5', 'D6', 'D7', 'VREF-', 'SC', 'OE', 'VGG', 'EOC', 'VSS', 'CLK', 'Vin', 'D0', 'D1', 'VREF+',
                    'D2', 'D3', 'VDD'],
         'shape': {'type': 'box', 'width': 140, 'pitch': 30,
                   'left': [{'name': f'D{i}', 'dir': 'out'} for i in (4, 5, 6, 7)] + ['VREF-', 'SC', 'OE', 'VGG',
                                                                                     {'name': 'EOC', 'dir': 'out'}],
                   'right': ['VDD', {'name': 'D3', 'dir': 'out'}, {'name': 'D2', 'dir': 'out'}, 'VREF+',
                             {'name': 'D1', 'dir': 'out'}, {'name': 'D0', 'dir': 'out'}, 'Vin', 'CLK', 'VSS'],
                   'leftStart': -90, 'rightStart': -90, 'title': 'ADC0800'},
         'spice': sp(N.join(ADC_LINES)),
         'description': '8-bit analog-to-digital converter with the ADC0800 pinout (analog simulation): '
                        'D = (Vin - VREF-) / (VREF+ - VREF-) x 256, converting continuously'},
        {'id': 'sip2', 'name': 'SIP2', 'prefix': 'J', 'aliases': ['SIP2'], 'package': 'SIP2', 'cmPins': ['1', '2'],
         'description': 'Two-pin connector (for the PCB netlist; no electrical model)',
         'graphics': [R(-10, -20, 20, 50), L(-20, -10, -10, -10), L(-20, 20, -10, 20), C(0, -10, 3), C(0, 20, 3)],
         'pins': [pin('1', -20, -10), pin('2', -20, 20)]},
        {'id': 'caution', 'name': 'Caution', 'prefix': 'X', 'aliases': ['Caution'], 'cmPins': [],
         'description': 'Caution sign (drawing only)',
         'graphics': [P('M 0 -30 L 32 26 L -32 26 Z'), L(0, -12, 0, 10), C(0, 17, 2, fill=True)], 'pins': []},
    ]


def power_parts():
    coil = lambda x0, y0, y1: [P(f'M {x0} {y0} ' + ' '.join(f'Q {x0 + 12} {y0 + (i + .5) * (y1 - y0) / 4} {x0} {y0 + (i + 1) * (y1 - y0) / 4}'
                                                           for i in range(4)))]
    return [
        {'id': '555', 'name': '555 Timer', 'prefix': 'U', 'label': '555', 'aliases': ['555 Timer', 'NE555', 'LM555'],
         'cmPins': ['GND', 'TRG', 'OUT', '~RST', 'CTL', 'THR', 'DIS', 'VCC'],
         'shape': {'type': 'box', 'width': 160, 'pitch': 30, 'left': ['GND', 'TRG', 'OUT', '~RST'],
                   'right': ['VCC', 'DIS', 'THR', 'CTL'], 'leftStart': -30, 'rightStart': -30, 'title': '555'},
         'spice': sp('X{name} {GND} {TRG} {OUT} {~RST} {CTL} {THR} {DIS} {VCC} TIMER555', subckt=TIMER555),
         'description': 'Timer: OUT goes high when TRG < VCC/3 and low when THR > 2/3 VCC; DIS shorts to GND while OUT is low'},
        {'id': 'relay_spdt', 'name': 'SPDT Relay', 'prefix': 'K', 'label': '100', 'aliases': ['SPDT Relay', 'Relay'],
         'cmPins': ['COM', 'NC', 'NO', 'C1', 'C2'], 'display': 'button', 'model': {'kind': 'relay', 'type': 'spdt'},
         # CircuitMaker's XSPDTRELAY: NO closes above 98 % of the pull-in voltage,
         # NC opens until the coil falls below pull-in less drop-off
         'props': {'pullin': '4', 'dropoff': '0.1', 'resistance': '500', 'inductance': '10m'},
         'spice': {'line': N.join(['L{name} {C1} {name}_a {prop:inductance}', 'R{name} {name}_a {C2} {prop:resistance}',
                                   'B{name}o {name}_o 0 V=abs(v({name}_a,{C2}))/(0.98*{prop:pullin})',
                                   'B{name}c {name}_c 0 V={prop:pullin}-{prop:dropoff}-abs(v({name}_a,{C2}))',
                                   'S{name}a {COM} {NO} {name}_o 0 RELAY_ON1', 'S{name}b {COM} {NC} {name}_c 0 RELAY_ON0']),
                   'models': ['.model RELAY_ON1 sw vt=1 ron=1m roff=1G', '.model RELAY_ON0 sw vt=0 ron=1m roff=1G']},
         'description': 'Relay: the coil (C1 positive, resistance and inductance in the properties) pulls COM from NC '
                        'to NO above the pull-in voltage; digitally, when C1 and C2 differ',
         'graphics': [C(30, -60, 3), C(20, -80, 3), C(20, -40, 3),
                      L(33, -60, 50, -60), L(23, -80, 50, -80), L(23, -40, 50, -40),
                      R(-40, 0, 30, 40), L(-35, 5, -15, 35), L(-50, -20, -25, -20), L(-25, -20, -25, 0),
                      L(-25, 40, -25, 60), L(-50, 60, -25, 60), P('M -10 20 L 10 20 L 24 -52')],
         'pins': [pin('COM', 50, -60), pin('NC', 50, -80), pin('NO', 50, -40), pin('C1', -50, -20, 'in'),
                  pin('C2', -50, 60, 'in')]},
        {'id': 'transformer', 'name': 'Transformer', 'prefix': 'T', 'label': '1:1', 'aliases': ['Transformer'],
         'cmPins': ['P1', 'P2', 'S1', 'S2'], 'props': {'lp': '10m', 'ls': '10m'},
         'spice': sp('L{name}p {P1} {name}_p {prop:lp}\nR{name}p {name}_p {P2} 0.1\n'
                     'L{name}s {S1} {name}_s {prop:ls}\nR{name}s {name}_s {S2} 0.1\nK{name} L{name}p L{name}s 0.999'),
         'description': 'Coupled inductors (k = 0.999, 0.1 ohm windings): voltage ratio = sqrt(ls / lp)',
         'graphics': coil(-20, -30, 30) + [P('M 20 -30 Q 8 -22.5 20 -15 Q 8 -7.5 20 0 Q 8 7.5 20 15 Q 8 22.5 20 30')]
                     + [L(-4, -30, -4, 30), L(4, -30, 4, 30), L(-40, -30, -20, -30), L(-40, 30, -20, 30),
                        L(20, -30, 40, -30), L(20, 30, 40, 30)],
         'pins': [pin('P1', -40, -30), pin('P2', -40, 30), pin('S1', 40, -30), pin('S2', 40, 30)]},
        {'id': 'ct_transformer', 'name': 'CT Transformer', 'prefix': 'T', 'label': 'CT', 'aliases': ['CTTransformer'],
         'cmPins': ['P1', 'P2', 'S1', 'CT', 'S2'], 'props': {'lp': '10m', 'ls': '2.5m'},
         'spice': sp('L{name}p {P1} {name}_p {prop:lp}\nR{name}p {name}_p {P2} 0.1\n'
                     'L{name}a {S1} {name}_a {prop:ls}\nR{name}a {name}_a {CT} 0.1\n'
                     'L{name}b {CT} {name}_b {prop:ls}\nR{name}b {name}_b {S2} 0.1\n'
                     'K{name}1 L{name}p L{name}a 0.999\nK{name}2 L{name}p L{name}b 0.999\nK{name}3 L{name}a L{name}b 0.999'),
         # pins where CircuitMaker's CTTransformer has them: primary left, secondary right
         'description': 'Centre-tapped transformer: primary P1-P2 (left), secondary halves S1-CT and CT-S2 of ls each',
         'graphics': coil(-30, -30, 20)
                     + [P('M 20 -60 ' + ' '.join(f'Q 8 {-60 + (i + .5) * 13.75} 20 {-60 + (i + 1) * 13.75}' for i in range(8)))]
                     + [L(-4, -60, -4, 50), L(4, -60, 4, 50), L(-50, -30, -30, -30), L(-50, 20, -30, 20),
                        L(20, -60, 40, -60), L(20, 0, 40, 0), L(20, 50, 40, 50)],
         'pins': [pin('P1', -50, -30), pin('P2', -50, 20), pin('S1', 40, -60), pin('CT', 40, 0), pin('S2', 40, 50)]},
        {'id': 'optoisolator', 'name': 'Opto Isolator', 'prefix': 'U', 'label': '4N25', 'aliases': ['Opto Isolator'],
         'cmPins': ['A', 'C', 'COL', 'E'],
         'model': {'kind': 'opto'},     # digital: COL-E conduct while the LED is lit (A high, C low)
         'spice': sp('D{name} {A} {name}_k DLED\nV{name} {name}_k {C} 0\nQ{name} {COL} {name}_b {E} Q2N3904\n'
                     'F{name} {E} {name}_b V{name} 0.002', 'DLED', 'Q2N3904'),
         'description': 'LED and phototransistor: the LED current drives the base (CTR about 80 %)',
         'graphics': [R(-60, -60, 120, 120), L(-90, -40, -40, -40), L(-90, 40, -40, 40), L(-40, -40, -40, -12),
                      P('M -52 -12 L -28 -12 L -40 12 Z'), L(-52, 12, -28, 12), L(-40, 12, -40, 40),
                      L(-18, -6, 0, -6), P('M -4 -10 L 2 -6 L -4 -2'), L(-18, 6, 0, 6), P('M -4 2 L 2 6 L -4 10'),
                      L(18, -20, 18, 20), L(18, -8, 40, -30), L(40, -30, 40, -40), L(18, 8, 40, 30), L(40, 30, 40, 40),
                      L(40, -40, 90, -40), L(40, 40, 90, 40), P('M 31 27 L 40 30 L 36 21 Z')],
         'pins': [pin('A', -90, -40), pin('C', -90, 40), pin('COL', 90, -40), pin('E', 90, 40)]},
        {'id': 'scr', 'name': 'SCR', 'prefix': 'Q', 'label': 'SCR', 'aliases': ['SCR'],
         'cmPins': ['A', 'G', 'K'], 'spice': sp('X{name} {A} {G} {K} SCR_GEN', subckt=SCR),
         'description': 'Thyristor: a gate current turns it on; it stays on until the anode current stops',
         'graphics': [L(0, -40, 0, -12), P('M -14 -12 L 14 -12 L 0 12 Z'), L(-14, 12, 14, 12), L(0, 12, 0, 40),
                      L(0, 12, -20, 20), L(-20, 20, -40, 20)],
         'pins': [pin('A', 0, -40), pin('G', -40, 20, 'in'), pin('K', 0, 40)]},
        {'id': 'triac', 'name': 'TRIAC', 'prefix': 'Q', 'label': 'TRIAC', 'aliases': ['Triac:A', 'Triac:B', 'Triac'],
         'cmPins': ['MT2', 'G', 'MT1'], 'spice': sp('X{name} {MT2} {G} {MT1} TRIAC_GEN', subckt=TRIAC),
         'description': 'Bidirectional thyristor: gate current of either sign turns it on in either direction',
         'graphics': [L(0, -60, 0, -14), L(-18, -14, 18, -14), L(-18, 14, 18, 14), P('M -18 -14 L -2 14 L -16 14 Z'),
                      P('M 2 -14 L 18 -14 L 10 14 Z'), L(0, 14, 0, 60), L(-10, 14, -30, 30), L(-30, 30, -50, 30)],
         'pins': [pin('MT2', 0, -60), pin('G', -50, 30, 'in'), pin('MT1', 0, 60)]},
    ]


def data_sequencer():
    # CircuitMaker's layout: outputs Q7 (pin 1) to Q0 on the right, clocks low on the left
    s = cm_chip('dataseq', 'Data Sequencer', [clk('CP1'), clk('CP2')], [f'Q{i}' for i in range(7, -1, -1)],
                {'kind': 'dataseq'}, [f'Q{i}' for i in range(7, -1, -1)] + ['CP1', 'CP2'], 90, -90,
                aliases=['Data Seq'], title='DATA SEQ')
    s['shape']['titleY'] = -120
    s.update({'name': 'Data Sequencer', 'prefix': 'DS', 'display': 'dataseq', 'label': '', 'floatInputs': 0,
              'props': {'data': '', 'lines': 32, 'start': 0, 'stop': 31, 'tick': 1, 'external': 0,
                        'low': '0', 'high': '5', 'step': '1m'},
              'description': 'Word generator: outputs 8-bit words from address start to stop and repeats. '
                             'Advances every tick increment, or on CP1 rising / CP2 falling edges with the external clock'})
    return s


def inputs():
    return [
        {'id': 'logicswitch', 'name': 'Logic Switch', 'prefix': 'S', 'display': 'logicswitch',
         'aliases': ['Logic Switch'], 'cmPins': ['Y'], 'model': {'kind': 'logicswitch'}, 'props': {'state': 0},
         'description': 'Click during simulation to toggle 0/1',
         'graphics': [L(-20, -11, -4, -11), C(-10, 10, 4), L(-20, 10, -14, 10), L(20, 0, 40, 0),
                      T('1', -24, -11, 7, 'right'), T('0', -24, 10, 7, 'right')],
         'pins': [pin('Y', 40, 0, 'out')]},
        data_sequencer(),
        {'id': 'pulser', 'name': 'Pulser', 'prefix': 'P', 'display': 'pulser', 'aliases': ['Pulser'],
         'cmPins': ['', '', 'Q', '~Q'], 'model': {'kind': 'pulser'}, 'props': {'high': 10, 'low': 10},
         'description': 'Clock source; high and low durations in ticks',
         # pins where CircuitMaker has them: Q upper right, ~Q further out at mid height
         'graphics': [R(-60, -50, 120, 80),
                      P('M -45 10 L -30 10 L -30 -25 L -5 -25 L -5 10 L 20 10 L 20 -25 L 45 -25 L 45 10'),
                      L(60, -30, 80, -30), C(65, 0, 5), L(70, 0, 100, 0),
                      T('Q', 54, -30, 7, 'right')],
         'pins': [pin('Q', 80, -30, 'out'), pin('~Q', 100, 0, 'out')]},
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
         'graphics': [C(15, 0, 3), L(18, 0, 40, 0), C(-15, 0, 3), L(-30, 0, -18, 0), C(-15, 50, 3), L(-30, 50, -18, 50),
                      T('NC', -15, -9, 6), T('NO', -15, 59, 6)],
         'pins': [pin('C', 40, 0), pin('NC', -30, 0), pin('NO', -30, 50)]},   # NO 14 units below NC, as in CircuitMaker
        {'id': 'spdt', 'name': 'SPDT Switch', 'prefix': 'S', 'display': 'spst', 'aliases': ['SPDT Switch'],
         'cmPins': ['C', 'NC', 'NO'], 'model': {'kind': 'link', 'type': 'spdt'}, 'props': {'closed': 0},
         'description': 'Change-over switch: click during simulation to move C from NC to NO and back',
         'graphics': [C(15, 0, 3), L(18, 0, 30, 0), C(-15, -10, 3), L(-30, -10, -18, -10), C(-15, 10, 3),
                      L(-30, 10, -18, 10)],
         'pins': [pin('C', 30, 0), pin('NC', -30, -10), pin('NO', -30, 10)]},
        {'id': 'car', 'name': 'Car', 'prefix': 'CAR', 'display': 'car', 'aliases': ['Car'],
         'cmPins': ['S', 'F', 'RESET'], 'model': {'kind': 'car'}, 'props': {'speed': 3, 'color': 'red'},
         'choices': {'color': ['red', 'green', 'yellow', 'blue', 'cyan', 'magenta']},
         'description': 'Animated race car: drives along its track once S goes high (faster with a higher speed), '
                        'raises F at the finish and goes back to the start while RESET is high',
         'graphics': [R(-90, -45, 330, 70), L(-90, -10, 240, -10), L(220, -45, 220, 25), L(-110, -20, -90, -20),
                      L(-110, 10, -90, 10), L(20, 25, 20, 80), T('S', -84, -20, 7, 'left'), T('F', -84, 10, 7, 'left'),
                      T('RESET', 20, 18, 6)],
         'pins': [pin('S', -110, -20, 'in'), pin('F', -110, 10, 'out'), pin('RESET', 20, 80, 'in')]},
        {'id': 'rocket', 'name': 'Rocket', 'prefix': 'R', 'display': 'rocket', 'aliases': ['Rocket'],
         'cmPins': ['+', '-'], 'model': {'kind': 'rocket'},
         'description': 'Animated rocket: lifts off when + goes high against -, and is back on its pad once the '
                        'input drops after the flight',
         'graphics': [R(-70, 20, 120, 60), L(-90, 30, -70, 30), L(-90, 60, -70, 60), T('+', -64, 30, 7, 'left'),
                      T('-', -64, 60, 7, 'left'), L(40, -260, 40, 20), L(30, -260, 50, -260)],
         'pins': [pin('+', -90, 30, 'in'), pin('-', -90, 60, 'in')]},
        {'id': 'stepper', 'name': 'Stepper Motor', 'prefix': 'M', 'display': 'stepper', 'label': '100H',
         'aliases': ['Stepper'], 'cmPins': ['1', 'COMA', '2', '3', 'COMB', '4'], 'model': {'kind': 'stepper'},
         # each coil: the inductance and 10 ohm, with 1k across it so that
         # switching it off does not raise an endless spike
         'spice': sp(N.join(f'L{{name}}{c} {{{c}}} {{name}}_{c} {{value}}' + N + f'R{{name}}{c} {{name}}_{c} {{{com}}} 10'
                            + N + f'R{{name}}{c}p {{{c}}} {{{com}}} 1k'
                            for c, com in (('1', 'COMA'), ('2', 'COMA'), ('3', 'COMB'), ('4', 'COMB')))),
         'description': 'Unipolar stepper motor: coils 1 and 2 on COMA, 3 and 4 on COMB (each the label inductance '
                        'and 10 ohm); the rotor turns to the energized coils: 1, 3, 2, 4 in sequence is a full turn',
         'graphics': [C(0, 0, 70), L(-100, -100, -60, -100), L(-60, -100, -45, -55), L(-100, -60, -70, -60),
                      L(-100, 60, -70, 60), L(-100, 100, -60, 100), L(-60, 100, -45, 55), L(100, -80, 60, -80),
                      L(60, -80, 50, -50), L(100, 80, 60, 80), L(60, 80, 50, 50),
                      T('1', -92, -108, 6, 'left'), T('2', -92, -68, 6, 'left'), T('3', -92, 52, 6, 'left'),
                      T('4', -92, 92, 6, 'left'), T('A', 92, -88, 6, 'right'), T('B', 92, 72, 6, 'right')],
         'pins': [pin('1', -100, -100, 'in', float=-1), pin('COMA', 100, -80, 'in', float=-1), pin('2', -100, -60, 'in', float=-1),
                  pin('3', -100, 60, 'in', float=-1), pin('COMB', 100, 80, 'in', float=-1), pin('4', -100, 100, 'in', float=-1)]},
        {'id': 'window', 'name': 'Window', 'prefix': 'W', 'display': 'window', 'aliases': ['Window'],
         'cmPins': ['C', 'NO', 'NC', 'OC'], 'model': {'kind': 'link', 'type': 'spdt'}, 'props': {'closed': 0},
         'description': 'Burglar-alarm window: click during simulation to open or shut it; shut, C connects to NC, '
                        'open, to NO (OC is not connected)',
         'graphics': [R(-110, -130, 220, 250), R(-95, -115, 170, 220), L(110, -100, 130, -100), L(110, 30, 130, 30),
                      L(110, 60, 130, 60), L(110, 90, 130, 90), T('OC', 104, -100, 8, 'right'),
                      T('NO', 104, 30, 8, 'right'), T('C', 104, 60, 8, 'right'), T('NC', 104, 90, 8, 'right')],
         'pins': [pin('C', 130, 60), pin('NO', 130, 30), pin('NC', 130, 90), pin('OC', 130, -100)]},
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
        g += [L(x, 70, x, 120), T(n, x + 6, 92, 6, 'left')]
    # pin ends where CircuitMaker has them (36 of its units below and above)
    g += [L(0, -80, 0, -120), T('COM', 12, -100, 6, 'left')]
    pins = [pin(n, -70 + 20 * i, 120, 'in') for i, n in enumerate(names)] + [pin('COM', 0, -120, 'in')]
    return g, pins


def outputs():
    seg_g, seg_p = seg_graphics()
    # a floating segment (an open-collector 7447 output that is off) stays dark
    seg_p = [dict(p, float=-1) if p['name'] != 'COM' else p for p in seg_p]
    return [
        {'id': 'logicdisplay', 'name': 'Logic Display', 'prefix': 'L', 'display': 'lamp', 'aliases': ['Logic Display'],
         'cmPins': ['D0'], 'model': {'kind': 'display', 'bits': 1},
         'props': {'color': 'red'},
         'choices': {'color': ['red', 'green', 'yellow', 'blue', 'cyan', 'magenta', 'orange', 'white']},
         'description': 'Lamp showing the logic level of its input; colour in the properties',
         'graphics': [C(0, 0, 20), C(0, 0, 15), L(0, 20, 0, 60)], 'pins': [pin('D0', 0, 60, 'in')]},
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
         'cmPins': [f'D{6 - i}' for i in range(7)] + ['CP'], 'model': {'kind': 'asciiterm'},   # CM pin 1 (top) is bit 6
         'props': {'text': '', 'cursor': 0},
         'description': 'Terminal of 4 lines of 16 characters, as CircuitMaker\'s: when CP falls it writes the '
                        'character on D6..D0 at the cursor; CR starts a new line, BS erases the last character, '
                        'and the text scrolls up past the last line',
         'graphics': [R(-80, -120, 300, 250)] + [L(-110, -100 + 30 * i, -80, -100 + 30 * i) for i in range(7)]
                     + [L(-110, 110, -90, 110), C(-85, 110, 5)]
                     + [T(f'D{6 - i}', -96, -106 + 30 * i, 6) for i in range(7)] + [T('CP', -66, 110, 6)],
         'pins': [pin(f'D{6 - i}', -110, -100 + 30 * i, 'in') for i in range(7)] + [pin('CP', -110, 110, 'in')]},
        {'id': 'led', 'name': 'LED', 'prefix': 'D', 'display': 'led', 'aliases': ['LED'], 'cmPins': ['A', 'K'],
         'model': {'kind': 'led'}, 'spice': sp('D{name} {A} {K} DLED', 'DLED'),
         'props': {'color': 'red'}, 'choices': {'color': ['red', 'green', 'yellow', 'blue', 'orange', 'white']},
         'description': 'Lights when the anode is high and the cathode low; colour in the properties',
         'graphics': [L(0, -20, 0, -8), P('M -10 -8 L 10 -8 L 0 6 Z'), L(-10, 6, 10, 6), L(0, 6, 0, 10),
                      L(12, -8, 22, -18), P('M 17 -18 L 22 -18 L 22 -13'),
                      L(15, -1, 25, -11), P('M 20 -11 L 25 -11 L 25 -6')],
         'pins': [pin('A', 0, -20, 'in'), pin('K', 0, 10, 'in')]},   # 8 units apart in CircuitMaker
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
        {'id': 'port_in', 'name': 'Input', 'prefix': '', 'netLabel': True, 'display': 'port', 'label': 'IN',
         'aliases': ['Input'], 'cmPins': ['1'],
         'description': 'Macro input: becomes a pin on the left of the macro symbol; connects by name like a terminal',
         'graphics': [P('M -60 -10 L -10 -10 L 0 0 L -10 10 L -60 10 Z'), L(0, 0, 10, 0)], 'pins': [pin('1', 10, 0)]},
        {'id': 'port_out', 'name': 'Output', 'prefix': '', 'netLabel': True, 'display': 'port', 'label': 'OUT',
         'aliases': ['Output'], 'cmPins': ['1'],
         'description': 'Macro output: becomes a pin on the right of the macro symbol; connects by name like a terminal',
         'graphics': [P('M 0 -10 L 50 -10 L 60 0 L 50 10 L 0 10 Z'), L(-10, 0, 0, 0)], 'pins': [pin('1', -10, 0)]},
        {'id': 'terminal', 'name': 'Terminal', 'prefix': 'T', 'netLabel': True,
         'aliases': ['Terminal'], 'cmPins': ['1'],
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
               bottom=['~OE'], width=80, pitch=40, title='3-ST', aliases=['Quad 3-State'],
               cmPins=[f'A{i}' for i in range(4)] + [f'Y{i}' for i in range(4)] + ['~OE'],
               model={'kind': 'tribus', 'groups': [{'enable': 'OE', 'bits': [0, 1, 2, 3]}]},
               description='Four tri-state buffers with a shared active-low output enable (OE has a bubble in '
                           'CircuitMaker: high floats the outputs)')


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
    write('90_analog.json', 'Analog', analog() + math_blocks() + cm_devices())
    print('library written to', os.path.normpath(OUT))
