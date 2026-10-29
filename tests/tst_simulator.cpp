#include "TestUtil.h"

using namespace cm;
using namespace cm::test;
using sim::Logic;

class TestSimulator : public QObject {
    Q_OBJECT
private slots:
    void pulserToggles();
    void asyncJkCounterCounts();
    void pushButtonWithPullUp();
    void triStateBus();
};

void TestSimulator::pulserToggles()
{
    Circuit c;
    Component p;
    p.symbolId = QStringLiteral("pulser");
    p.props = {{QStringLiteral("high"), 10}, {QStringLiteral("low"), 10}};
    const ItemId id = c.addComponent(p);
    Bench bench(c);
    QCOMPARE(bench.value(c, id, QStringLiteral("Q")), Logic::L0);
    bench.sim.step(10);
    QCOMPARE(bench.value(c, id, QStringLiteral("Q")), Logic::L1);
    bench.sim.step(10);
    QCOMPARE(bench.value(c, id, QStringLiteral("Q")), Logic::L0);
}

// Four CD4027 JK flip-flops with J=K=1 in a ripple chain, as in the user's
// Contador_assincrono_JK_crescente circuit, drive a hex display 0..F.
void TestSimulator::asyncJkCounterCounts()
{
    Circuit c;
    Component p;
    p.symbolId = QStringLiteral("pulser");
    p.props = {{QStringLiteral("high"), 20}, {QStringLiteral("low"), 20}};
    p.pos = {-400, 0};
    const ItemId clk = c.addComponent(p);
    const ItemId vcc = place(c, QStringLiteral("vcc"), {-400, -400});
    const ItemId gnd = place(c, QStringLiteral("ground"), {-400, 400});
    const ItemId hex = place(c, QStringLiteral("hexdisplay"), {1500, -500}, QStringLiteral("D1"));
    label(c, vcc, QStringLiteral("VCC"), QStringLiteral("H"));
    label(c, gnd, QStringLiteral("GND"), QStringLiteral("L"));
    label(c, clk, QStringLiteral("Q"), QStringLiteral("C0"));

    for (int i = 0; i < 4; ++i) {
        const ItemId ff = place(c, QStringLiteral("jkff_sr"), {i * 300.0, 0}, QStringLiteral("U%1").arg(i + 1));
        label(c, ff, QStringLiteral("J"), QStringLiteral("H"));
        label(c, ff, QStringLiteral("K"), QStringLiteral("H"));
        label(c, ff, QStringLiteral("S"), QStringLiteral("L"));
        label(c, ff, QStringLiteral("R"), QStringLiteral("L"));
        // up-counter on rising edges: clock each stage from the previous ~Q
        label(c, ff, QStringLiteral("CLK"), QStringLiteral("C%1").arg(i));
        label(c, ff, QStringLiteral("~Q"), QStringLiteral("C%1").arg(i + 1));
        label(c, ff, QStringLiteral("Q"), QStringLiteral("B%1").arg(i));
        label(c, hex, QStringLiteral("D%1").arg(i), QStringLiteral("B%1").arg(i));
    }

    Bench bench(c);
    QVERIFY2(bench.sim.warnings().isEmpty(), qPrintable(bench.sim.warnings().join(u'\n')));
    QCOMPARE(bench.sim.componentState(hex), 0);
    for (int count = 1; count <= 20; ++count) {
        bench.sim.step(40);
        QCOMPARE(bench.sim.componentState(hex), count % 16);
    }
}

void TestSimulator::pushButtonWithPullUp()
{
    Circuit c;
    const ItemId vcc = place(c, QStringLiteral("vcc"), {0, -200});
    const ItemId gnd = place(c, QStringLiteral("ground"), {200, 200});
    const ItemId r = place(c, QStringLiteral("resistor"), {0, 0}, QStringLiteral("R1"));
    const ItemId pb = place(c, QStringLiteral("pb_no"), {200, 0}, QStringLiteral("S1"));
    const ItemId lamp = place(c, QStringLiteral("logicdisplay"), {100, -100}, QStringLiteral("L1"));
    link(c, vcc, QStringLiteral("VCC"), r, QStringLiteral("1"));
    link(c, r, QStringLiteral("2"), pb, QStringLiteral("1"));
    link(c, pb, QStringLiteral("2"), gnd, QStringLiteral("GND"));
    link(c, r, QStringLiteral("2"), lamp, QStringLiteral("D0"));

    Bench bench(c);
    QCOMPARE(bench.sim.componentState(lamp), 1);
    bench.sim.interact(pb, QStringLiteral("press"));
    QCOMPARE(bench.sim.componentState(lamp), 0);
    bench.sim.interact(pb, QStringLiteral("release"));
    QCOMPARE(bench.sim.componentState(lamp), 1);
}

void TestSimulator::triStateBus()
{
    Circuit c;
    const ItemId b1 = place(c, QStringLiteral("tribuf"), {0, 0}, QStringLiteral("U1"));
    const ItemId b2 = place(c, QStringLiteral("tribuf"), {0, 100}, QStringLiteral("U2"));
    const ItemId one = place(c, QStringLiteral("vcc"), {-100, -100});
    const ItemId zero = place(c, QStringLiteral("ground"), {-100, 200});
    const ItemId en1 = place(c, QStringLiteral("logicswitch"), {-200, -50}, QStringLiteral("S1"));
    const ItemId en2 = place(c, QStringLiteral("logicswitch"), {-200, 50}, QStringLiteral("S2"));
    link(c, one, QStringLiteral("VCC"), b1, QStringLiteral("A"));
    link(c, zero, QStringLiteral("GND"), b2, QStringLiteral("A"));
    link(c, en1, QStringLiteral("Y"), b1, QStringLiteral("EN"));
    link(c, en2, QStringLiteral("Y"), b2, QStringLiteral("EN"));
    link(c, b1, QStringLiteral("Y"), b2, QStringLiteral("Y"));

    Bench bench(c);
    QCOMPARE(bench.value(c, b1, QStringLiteral("Y")), Logic::Z);
    bench.sim.interact(en1, QStringLiteral("toggle"));
    bench.sim.step(2);
    QCOMPARE(bench.value(c, b1, QStringLiteral("Y")), Logic::L1);
    bench.sim.interact(en2, QStringLiteral("toggle"));
    bench.sim.step(2);
    QCOMPARE(bench.value(c, b1, QStringLiteral("Y")), Logic::X);    // bus contention
    bench.sim.interact(en1, QStringLiteral("toggle"));
    bench.sim.step(2);
    QCOMPARE(bench.value(c, b1, QStringLiteral("Y")), Logic::L0);
}

QTEST_GUILESS_MAIN(TestSimulator)
#include "tst_simulator.moc"
