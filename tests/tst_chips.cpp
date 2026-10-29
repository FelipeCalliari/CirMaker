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

QTEST_GUILESS_MAIN(TestChips)
#include "tst_chips.moc"
