#include "TestUtil.h"

using namespace cm;
using namespace cm::test;
using sim::Logic;

class TestChips : public QObject {
    Q_OBJECT
private slots:
    void decadeCounter7490();
    void decoder74138();
    void bcdTo7seg7447();
    void syncCounter74161();
    void adder7483();
    void upDown74191();
    void piso74165();
    void transceiver74245();
    void ramWriteRead();
    void promRead();
    void decoder74155();
    void parity74280();
    void keypadConnects();
    void dualDFlipFlop74LS74();
    void dualDecoder74139();
};

namespace {

// Drives an input pin from a logic switch named by `net`.
ItemId source(Circuit& c, ItemId chip, const QString& pin, double x, double y)
{
    const ItemId s = place(c, QStringLiteral("logicswitch"), {x, y});
    const QString net = QStringLiteral("n_%1_%2").arg(chip).arg(pin);
    label(c, s, QStringLiteral("Y"), net);
    label(c, chip, pin, net);
    return s;
}

ItemId tieLow(Circuit& c, ItemId chip, const QString& pin)
{
    const ItemId g = place(c, QStringLiteral("ground"), {-2000, 2000});
    label(c, g, QStringLiteral("GND"), QStringLiteral("LOW"));
    label(c, chip, pin, QStringLiteral("LOW"));
    return g;
}

QStringList names(const QString& prefix, int n)
{
    QStringList l;
    for (int i = 0; i < n; ++i) l << prefix + QString::number(i);
    return l;
}

void pulse(Bench& b, ItemId sw)
{
    b.sim.interact(sw, QStringLiteral("toggle"));
    b.sim.step(4);
    b.sim.interact(sw, QStringLiteral("toggle"));
    b.sim.step(4);
}

int bus(const Bench& b, const Circuit& c, ItemId chip, const QStringList& pins)
{
    int v = 0;
    for (int i = 0; i < pins.size(); ++i) {
        const Logic l = b.value(c, chip, pins[i]);
        if (l == Logic::X || l == Logic::Z) return -1;
        v |= int(l == Logic::L1) << i;
    }
    return v;
}

} // namespace

void TestChips::decadeCounter7490()
{
    // QA -> CKB external link makes a BCD counter
    Circuit c;
    const ItemId u = place(c, QStringLiteral("7490"), {0, 0}, QStringLiteral("U1"));
    const ItemId clk = place(c, QStringLiteral("logicswitch"), {-600, 0});
    label(c, clk, QStringLiteral("Y"), QStringLiteral("CK"));
    label(c, u, QStringLiteral("~CKA"), QStringLiteral("CK"));
    label(c, u, QStringLiteral("QA"), QStringLiteral("QA"));
    label(c, u, QStringLiteral("~CKB"), QStringLiteral("QA"));
    for (const char* r : {"R01", "R02", "R91", "R92"}) {
        const ItemId g = place(c, QStringLiteral("ground"), {-900, 900});
        label(c, g, QStringLiteral("GND"), QStringLiteral("LOW"));
        label(c, u, QString::fromLatin1(r), QStringLiteral("LOW"));
    }
    Bench b(c);
    const QStringList q{QStringLiteral("QA"), QStringLiteral("QB"), QStringLiteral("QC"), QStringLiteral("QD")};
    QCOMPARE(bus(b, c, u, q), 0);
    for (int n = 1; n <= 12; ++n) {
        b.sim.interact(clk, QStringLiteral("toggle"));   // rise
        b.sim.step(5);
        b.sim.interact(clk, QStringLiteral("toggle"));   // fall: count
        b.sim.step(5);
        QCOMPARE(bus(b, c, u, q), n % 10);
    }
}

void TestChips::decoder74138()
{
    Circuit c;
    const ItemId u = place(c, QStringLiteral("74138"), {0, 0}, QStringLiteral("U1"));
    const ItemId a = source(c, u, QStringLiteral("A"), -600, 0);
    const ItemId bsel = source(c, u, QStringLiteral("B"), -600, 100);
    const ItemId g1 = source(c, u, QStringLiteral("G1"), -600, 200);
    // ~G2A/~G2B and C left open: TTL inputs float high, so G2 must be grounded
    for (const char* n : {"~G2A", "~G2B", "C"}) {
        const ItemId g = place(c, QStringLiteral("ground"), {-900, 900});
        label(c, g, QStringLiteral("GND"), QStringLiteral("LOW"));
        label(c, u, QString::fromLatin1(n), QStringLiteral("LOW"));
    }
    Bench b(c);
    QCOMPARE(b.value(c, u, QStringLiteral("~Y0")), Logic::L1);     // disabled
    b.sim.interact(g1, QStringLiteral("toggle"));
    b.sim.step(3);
    QCOMPARE(b.value(c, u, QStringLiteral("~Y0")), Logic::L0);
    b.sim.interact(a, QStringLiteral("toggle"));
    b.sim.interact(bsel, QStringLiteral("toggle"));
    b.sim.step(3);
    QCOMPARE(b.value(c, u, QStringLiteral("~Y3")), Logic::L0);
    QCOMPARE(b.value(c, u, QStringLiteral("~Y0")), Logic::L1);
}

void TestChips::bcdTo7seg7447()
{
    Circuit c;
    const ItemId u = place(c, QStringLiteral("7447"), {0, 0}, QStringLiteral("U1"));
    const ItemId disp = place(c, QStringLiteral("seg7_ca"), {600, 0}, QStringLiteral("D1"));
    const ItemId vcc = place(c, QStringLiteral("vcc"), {900, -600});
    label(c, vcc, QStringLiteral("VCC"), QStringLiteral("VCC"));
    label(c, disp, QStringLiteral("COM"), QStringLiteral("VCC"));
    for (const QChar s : QStringLiteral("abcdefg")) {
        label(c, u, u'~' + QString(s), QString(s));
        label(c, disp, QString(s), QString(s));
    }
    QVector<ItemId> in;
    for (const char* n : {"A", "B", "C", "D"})
        in << source(c, u, QString::fromLatin1(n), -600, 100.0 * in.size());
    Bench b(c);
    QCOMPARE(b.sim.componentState(disp), 0x3f);                     // 0
    b.sim.interact(in[0], QStringLiteral("toggle"));
    b.sim.interact(in[1], QStringLiteral("toggle"));
    b.sim.step(4);
    QCOMPARE(b.sim.componentState(disp), 0x4f);                     // 3
}

void TestChips::syncCounter74161()
{
    Circuit c;
    const ItemId u = place(c, QStringLiteral("74161"), {0, 0}, QStringLiteral("U1"));
    const ItemId clk = source(c, u, QStringLiteral("CLK"), -600, 0);
    Bench b(c);
    const QStringList q{QStringLiteral("Q0"), QStringLiteral("Q1"), QStringLiteral("Q2"), QStringLiteral("Q3")};
    for (int n = 1; n <= 17; ++n) {
        b.sim.interact(clk, QStringLiteral("toggle"));
        b.sim.step(3);
        b.sim.interact(clk, QStringLiteral("toggle"));
        b.sim.step(3);
        QCOMPARE(bus(b, c, u, q), n % 16);
        QCOMPARE(b.value(c, u, QStringLiteral("RCO")), n % 16 == 15 ? Logic::L1 : Logic::L0);
    }
}

void TestChips::adder7483()
{
    Circuit c;
    const ItemId u = place(c, QStringLiteral("7483"), {0, 0}, QStringLiteral("U1"));
    QVector<ItemId> a, bb;
    for (int i = 0; i < 4; ++i) {
        a << source(c, u, QStringLiteral("A%1").arg(i), -600, 100.0 * i);
        bb << source(c, u, QStringLiteral("B%1").arg(i), -600, 500.0 + 100 * i);
    }
    const ItemId g = place(c, QStringLiteral("ground"), {-900, 1500});
    label(c, g, QStringLiteral("GND"), QStringLiteral("LOW"));
    label(c, u, QStringLiteral("CI"), QStringLiteral("LOW"));
    Bench b(c);
    // 11 + 6 = 17 -> S = 1, CO = 1
    for (int i : {0, 1, 3}) b.sim.interact(a[i], QStringLiteral("toggle"));
    for (int i : {1, 2}) b.sim.interact(bb[i], QStringLiteral("toggle"));
    b.sim.step(3);
    QCOMPARE(bus(b, c, u, {QStringLiteral("S0"), QStringLiteral("S1"), QStringLiteral("S2"), QStringLiteral("S3")}), 1);
    QCOMPARE(b.value(c, u, QStringLiteral("CO")), Logic::L1);
}

void TestChips::upDown74191()
{
    Circuit c;
    const ItemId u = place(c, QStringLiteral("74191"), {0, 0}, QStringLiteral("U1"));
    const ItemId clk = source(c, u, QStringLiteral("CLK"), -600, 0);
    const ItemId dir = source(c, u, QStringLiteral("D/~U"), -600, 100);
    tieLow(c, u, QStringLiteral("~CTEN"));
    Bench b(c);
    for (int i = 0; i < 3; ++i) pulse(b, clk);
    QCOMPARE(bus(b, c, u, names(QStringLiteral("Q"), 4)), 3);
    b.sim.interact(dir, QStringLiteral("toggle"));               // count down
    b.sim.step(3);
    for (int i = 0; i < 5; ++i) pulse(b, clk);
    QCOMPARE(bus(b, c, u, names(QStringLiteral("Q"), 4)), 14);  // 3 - 5 mod 16
}

void TestChips::piso74165()
{
    Circuit c;
    const ItemId u = place(c, QStringLiteral("74165"), {0, 0}, QStringLiteral("U1"));
    const ItemId clk = source(c, u, QStringLiteral("CLK"), -600, 0);
    const ItemId ld = source(c, u, QStringLiteral("~LD"), -600, 100);
    tieLow(c, u, QStringLiteral("INH"));
    tieLow(c, u, QStringLiteral("SER"));
    // parallel data 1000 0001: D0 and D7 high, others low
    for (int i = 1; i < 7; ++i) tieLow(c, u, QStringLiteral("D%1").arg(i));
    Bench b(c);                       // ~LD low (switch off): loading
    b.sim.step(3);
    QCOMPARE(b.value(c, u, QStringLiteral("QH")), Logic::L1);   // D7
    b.sim.interact(ld, QStringLiteral("toggle"));                // shift mode
    b.sim.step(3);
    QString bits;
    for (int i = 0; i < 8; ++i) {
        bits += b.value(c, u, QStringLiteral("QH")) == Logic::L1 ? u'1' : u'0';
        pulse(b, clk);
    }
    QCOMPARE(bits, QStringLiteral("10000001"));
}

void TestChips::transceiver74245()
{
    Circuit c;
    const ItemId u = place(c, QStringLiteral("74245"), {0, 0}, QStringLiteral("U1"));
    const ItemId dir = source(c, u, QStringLiteral("DIR"), -600, 0);
    const ItemId a0 = source(c, u, QStringLiteral("A0"), -600, 100);
    const ItemId b1 = source(c, u, QStringLiteral("B1"), 600, 100);
    tieLow(c, u, QStringLiteral("~G"));
    Bench b(c);
    b.sim.interact(b1, QStringLiteral("toggle"));
    b.sim.step(3);
    QCOMPARE(b.value(c, u, QStringLiteral("A1")), Logic::L1);   // DIR low: B -> A
    b.sim.interact(dir, QStringLiteral("toggle"));
    b.sim.interact(a0, QStringLiteral("toggle"));
    b.sim.step(3);
    QCOMPARE(b.value(c, u, QStringLiteral("B0")), Logic::L1);   // DIR high: A -> B
}

void TestChips::ramWriteRead()
{
    Circuit c;
    const ItemId u = place(c, QStringLiteral("RAM1K"), {0, 0}, QStringLiteral("U1"));
    const ItemId a0 = source(c, u, QStringLiteral("A0"), -600, 0);
    for (int i = 1; i < 10; ++i) tieLow(c, u, QStringLiteral("A%1").arg(i));
    tieLow(c, u, QStringLiteral("~CS"));
    const ItemId we = source(c, u, QStringLiteral("~WE"), -600, 100);       // low: write
    // a 74245 isolates the data source from the RAM while reading
    const ItemId t = place(c, QStringLiteral("74245"), {800, 0}, QStringLiteral("U2"));
    for (int i = 0; i < 8; ++i) label(c, t, QStringLiteral("A%1").arg(i), QStringLiteral("DB%1").arg(i)),
                                label(c, u, QStringLiteral("D%1").arg(i), QStringLiteral("DB%1").arg(i));
    const ItemId d5 = source(c, t, QStringLiteral("B5"), 1400, 0);
    const ItemId g = source(c, t, QStringLiteral("~G"), 1400, 100);         // low: transceiver on
    tieLow(c, t, QStringLiteral("DIR"));                                      // B -> A
    Bench b(c);
    b.sim.interact(d5, QStringLiteral("toggle"));
    b.sim.step(5);
    QCOMPARE(bus(b, c, u, names(QStringLiteral("D"), 8)), 0x20);
    b.sim.interact(g, QStringLiteral("toggle"));      // isolate
    b.sim.interact(we, QStringLiteral("toggle"));     // read
    b.sim.step(5);
    QCOMPARE(bus(b, c, u, names(QStringLiteral("D"), 8)), 0x20);
    b.sim.interact(a0, QStringLiteral("toggle"));     // other address still 0
    b.sim.step(5);
    QCOMPARE(bus(b, c, u, names(QStringLiteral("D"), 8)), 0x00);
}

void TestChips::promRead()
{
    Circuit c;
    Component prom;
    prom.symbolId = QStringLiteral("PROM32");
    prom.designator = QStringLiteral("U1");
    prom.props = {{QStringLiteral("data"), QStringLiteral("11 22 33 44")}};
    const ItemId u = c.addComponent(prom);
    const ItemId a0 = source(c, u, QStringLiteral("A0"), -600, 0);
    const ItemId a1 = source(c, u, QStringLiteral("A1"), -600, 100);
    for (int i = 2; i < 5; ++i) tieLow(c, u, QStringLiteral("A%1").arg(i));
    const ItemId cs = source(c, u, QStringLiteral("~CS"), -600, 200);
    Bench b(c);
    QCOMPARE(bus(b, c, u, names(QStringLiteral("D"), 8)), 0x11);
    b.sim.interact(a0, QStringLiteral("toggle"));
    b.sim.interact(a1, QStringLiteral("toggle"));
    b.sim.step(3);
    QCOMPARE(bus(b, c, u, names(QStringLiteral("D"), 8)), 0x44);
    b.sim.interact(cs, QStringLiteral("toggle"));     // deselect: outputs float
    b.sim.step(3);
    QCOMPARE(b.value(c, u, QStringLiteral("D0")), Logic::Z);
}

void TestChips::decoder74155()
{
    Circuit c;
    const ItemId u = place(c, QStringLiteral("74155"), {0, 0}, QStringLiteral("U1"));
    const ItemId a = source(c, u, QStringLiteral("A"), -600, 0);
    const ItemId c1 = source(c, u, QStringLiteral("1C"), -600, 100);
    for (const char* n : {"B", "~1G", "~2G", "~2C"}) tieLow(c, u, QString::fromLatin1(n));
    Bench b(c);
    QCOMPARE(b.value(c, u, QStringLiteral("~2Y0")), Logic::L0);   // section 2 enabled, select 0
    QCOMPARE(b.value(c, u, QStringLiteral("~1Y0")), Logic::L1);   // 1C low disables section 1
    b.sim.interact(c1, QStringLiteral("toggle"));
    b.sim.interact(a, QStringLiteral("toggle"));
    b.sim.step(3);
    QCOMPARE(b.value(c, u, QStringLiteral("~1Y1")), Logic::L0);
    QCOMPARE(b.value(c, u, QStringLiteral("~2Y1")), Logic::L0);
    QCOMPARE(b.value(c, u, QStringLiteral("~2Y0")), Logic::L1);
}

void TestChips::parity74280()
{
    Circuit c;
    const ItemId u = place(c, QStringLiteral("74280"), {0, 0}, QStringLiteral("U1"));
    const ItemId i0 = source(c, u, QStringLiteral("I0"), -600, 0);
    for (int i = 1; i < 9; ++i) tieLow(c, u, QStringLiteral("I%1").arg(i));
    Bench b(c);
    QCOMPARE(b.value(c, u, QStringLiteral("EVEN")), Logic::L1);
    b.sim.interact(i0, QStringLiteral("toggle"));
    b.sim.step(3);
    QCOMPARE(b.value(c, u, QStringLiteral("ODD")), Logic::L1);
    QCOMPARE(b.value(c, u, QStringLiteral("EVEN")), Logic::L0);
}

void TestChips::keypadConnects()
{
    Circuit c;
    const ItemId k = place(c, QStringLiteral("keypad4x4"), {0, 0}, QStringLiteral("K1"));
    const ItemId vcc = place(c, QStringLiteral("vcc"), {-600, -600});
    label(c, vcc, QStringLiteral("VCC"), QStringLiteral("H"));
    label(c, k, QStringLiteral("R1"), QStringLiteral("H"));
    const ItemId lamp = place(c, QStringLiteral("logicdisplay"), {600, 600});
    label(c, lamp, QStringLiteral("D0"), QStringLiteral("COL2"));
    label(c, k, QStringLiteral("C2"), QStringLiteral("COL2"));
    Bench b(c);
    QCOMPARE(b.sim.componentState(lamp), 0);
    b.sim.interact(k, QStringLiteral("key"), 1 * 4 + 2);
    QCOMPARE(b.sim.componentState(lamp), 1);
    b.sim.interact(k, QStringLiteral("release"));
    QCOMPARE(b.sim.componentState(lamp), 0);
}

// Both halves of the whole-package 74LS74 work independently.
void TestChips::dualDFlipFlop74LS74()
{
    Circuit c;
    const ItemId u = place(c, QStringLiteral("74LS74"), {0, 0}, QStringLiteral("U1"));
    const ItemId d1 = source(c, u, QStringLiteral("1D"), -600, 0);
    const ItemId k1 = source(c, u, QStringLiteral("1CLK"), -600, 100);
    const ItemId d2 = source(c, u, QStringLiteral("2D"), -600, 200);
    const ItemId k2 = source(c, u, QStringLiteral("2CLK"), -600, 300);
    Bench b(c);         // preset/clear float high (inactive)
    QCOMPARE(b.value(c, u, QStringLiteral("1Q")), Logic::L0);
    b.sim.interact(d1, QStringLiteral("toggle"));
    b.sim.step(4);
    pulse(b, k1);
    QCOMPARE(b.value(c, u, QStringLiteral("1Q")), Logic::L1);
    QCOMPARE(b.value(c, u, QStringLiteral("~1Q")), Logic::L0);
    QCOMPARE(b.value(c, u, QStringLiteral("2Q")), Logic::L0);     // other half untouched
    b.sim.interact(d2, QStringLiteral("toggle"));
    b.sim.step(4);
    pulse(b, k2);
    QCOMPARE(b.value(c, u, QStringLiteral("2Q")), Logic::L1);
}

void TestChips::dualDecoder74139()
{
    Circuit c;
    const ItemId u = place(c, QStringLiteral("74139"), {0, 0}, QStringLiteral("U1"));
    const ItemId a1 = source(c, u, QStringLiteral("1A"), -600, 0);
    const ItemId b2 = source(c, u, QStringLiteral("2B"), -600, 100);
    for (const char* n : {"~1G", "~2G", "1B", "2A"}) tieLow(c, u, QString::fromLatin1(n));
    Bench b(c);
    QCOMPARE(bus(b, c, u, {"~1Y0", "~1Y1", "~1Y2", "~1Y3"}), 0b1110);
    QCOMPARE(bus(b, c, u, {"~2Y0", "~2Y1", "~2Y2", "~2Y3"}), 0b1110);
    b.sim.interact(a1, QStringLiteral("toggle"));
    b.sim.interact(b2, QStringLiteral("toggle"));
    b.sim.step(4);
    QCOMPARE(bus(b, c, u, {"~1Y0", "~1Y1", "~1Y2", "~1Y3"}), 0b1101);     // 1: A=1 -> Y1
    QCOMPARE(bus(b, c, u, {"~2Y0", "~2Y1", "~2Y2", "~2Y3"}), 0b1011);     // 2: B=1 -> Y2
}

QTEST_GUILESS_MAIN(TestChips)
#include "tst_chips.moc"
