#include "TestUtil.h"

using namespace cm;
using namespace cm::test;
using sim::Logic;

class TestSimulator : public QObject {
    Q_OBJECT
private slots:
    void pulserToggles();
    void ramKeepsWrites();
    void pulserTriggered();
    void animatedDevices();
    void asyncJkCounterCounts();
    void pushButtonWithPullUp();
    void triStateBus();
    void probeInjects();
    void dataSequencerInternalClock();
    void dataSequencerExternalClock();
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

// What a RAM holds when the simulation stops stays in the part (its data),
// so it is there when the simulation is stopped and the memory opened
void TestSimulator::ramKeepsWrites()
{
    Circuit c;
    const ItemId ram = place(c, QStringLiteral("RAM1K"), {0, 0}, QStringLiteral("U1"));
    Bench bench(c);
    QVERIFY(bench.sim.persistentProps(ram).isEmpty());          // unchanged: nothing to keep
    QVector<int> words = bench.sim.memory(ram);
    QCOMPARE(words.size(), 1024);
    words[0] = 0x12;
    words[2] = 0xAB;
    bench.sim.setMemory(ram, words);
    QCOMPARE(bench.sim.persistentProps(ram).value(QStringLiteral("data")).toString(), QStringLiteral("12 00 AB"));
}

// CircuitMaker's External Trigger: a rise on CP1 makes the Pulser active for
// its high time from the next tick; Invert swaps Q1 and Q2
void TestSimulator::pulserTriggered()
{
    Circuit c;
    Component p;
    p.symbolId = QStringLiteral("pulser");
    p.props = {{QStringLiteral("high"), 10}, {QStringLiteral("low"), 10}, {QStringLiteral("trigger"), true}};
    const ItemId id = c.addComponent(p);
    const ItemId sw = place(c, QStringLiteral("logicswitch"), {-400, -30}, QStringLiteral("S1"));
    link(c, sw, QStringLiteral("Y"), id, QStringLiteral("CP1"));
    Bench bench(c);
    bench.sim.step(40);
    QCOMPARE(bench.value(c, id, QStringLiteral("Q")), Logic::L0);   // waits for a trigger
    bench.sim.interact(sw, QStringLiteral("toggle"));
    bench.sim.step(3);
    QCOMPARE(bench.value(c, id, QStringLiteral("Q")), Logic::L1);
    QCOMPARE(bench.value(c, id, QStringLiteral("~Q")), Logic::L0);
    bench.sim.step(15);
    QCOMPARE(bench.value(c, id, QStringLiteral("Q")), Logic::L0);   // for one high time
    bench.sim.step(40);
    QCOMPARE(bench.value(c, id, QStringLiteral("Q")), Logic::L0);

    Circuit c2;
    p.props = {{QStringLiteral("high"), 10}, {QStringLiteral("low"), 10}, {QStringLiteral("inverted"), true}};
    const ItemId id2 = c2.addComponent(p);
    Bench bench2(c2);
    QCOMPARE(bench2.value(c2, id2, QStringLiteral("Q")), Logic::L1);
    bench2.sim.step(10);
    QCOMPARE(bench2.value(c2, id2, QStringLiteral("Q")), Logic::L0);
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

// Probe Tool injection: an undriven node takes the level (or Z) until
// something drives it; a driven node keeps its level.
void TestSimulator::probeInjects()
{
    Circuit c;
    const ItemId inv = place(c, QStringLiteral("inv"), {0, 0}, QStringLiteral("U1"));
    const ItemId sw = place(c, QStringLiteral("logicswitch"), {-300, 0}, QStringLiteral("S1"));
    const ItemId inv2 = place(c, QStringLiteral("inv"), {0, 300}, QStringLiteral("U2"));
    link(c, sw, QStringLiteral("Y"), inv2, QStringLiteral("A"));
    Bench b(c);
    const SymbolDef* s = library().find(QStringLiteral("inv"));
    const int in = b.netlist.netOf(inv, s->pinIndex(QStringLiteral("A")));
    QVERIFY(b.sim.inject(in, Logic::L1));
    b.sim.step(2);
    QCOMPARE(b.value(c, inv, QStringLiteral("Y")), Logic::L0);
    QVERIFY(b.sim.inject(in, Logic::L0));
    b.sim.step(2);
    QCOMPARE(b.value(c, inv, QStringLiteral("Y")), Logic::L1);
    QVERIFY(b.sim.inject(in, Logic::Z));
    QCOMPARE(b.value(c, inv, QStringLiteral("A")), Logic::Z);
    const int driven = b.netlist.netOf(inv2, s->pinIndex(QStringLiteral("A")));
    QVERIFY(!b.sim.inject(driven, Logic::L1));          // the switch drives it low
    QCOMPARE(b.value(c, inv2, QStringLiteral("A")), Logic::L0);
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
    // bus contention: as in CircuitMaker, the device that comes last (U2) wins
    QCOMPARE(bench.value(c, b1, QStringLiteral("Y")), Logic::L0);
    QVERIFY(bench.sim.contention());
    bench.sim.interact(en1, QStringLiteral("toggle"));
    bench.sim.step(2);
    QCOMPARE(bench.value(c, b1, QStringLiteral("Y")), Logic::L0);

    // optionally, contention is unknown
    Bench strict(c);
    strict.sim.setConflictsUnknown(true);
    strict.sim.interact(en1, QStringLiteral("toggle"));
    strict.sim.interact(en2, QStringLiteral("toggle"));
    strict.sim.step(2);
    QCOMPARE(strict.value(c, b1, QStringLiteral("Y")), Logic::X);
}

void TestSimulator::dataSequencerInternalClock()
{
    Circuit c;
    Component d;
    d.symbolId = QStringLiteral("dataseq");
    d.props = {{QStringLiteral("data"), QStringLiteral("AA 55 0F F0")}, {QStringLiteral("lines"), 4},
               {QStringLiteral("start"), 1}, {QStringLiteral("stop"), 3}, {QStringLiteral("tick"), 5}};
    const ItemId id = c.addComponent(d);
    Bench bench(c);
    auto word = [&] {
        int v = 0;
        for (int b = 0; b < 8; ++b)
            v |= int(bench.value(c, id, QStringLiteral("Q%1").arg(b)) == Logic::L1) << b;
        return v;
    };
    // starts at the start address, advances every 5 ticks, wraps to start
    for (int expected : {0x55, 0x0F, 0xF0, 0x55, 0x0F}) {
        QCOMPARE(word(), expected);
        bench.sim.step(5);
    }
}

void TestSimulator::dataSequencerExternalClock()
{
    Circuit c;
    Component d;
    d.symbolId = QStringLiteral("dataseq");
    d.props = {{QStringLiteral("data"), QStringLiteral("01 02 03")}, {QStringLiteral("lines"), 3},
               {QStringLiteral("stop"), 2}, {QStringLiteral("external"), 1}};
    const ItemId id = c.addComponent(d);
    Component p;
    p.symbolId = QStringLiteral("pulser");
    p.pos = {-400, 0};
    p.props = {{QStringLiteral("high"), 10}, {QStringLiteral("low"), 10}};
    const ItemId clk = c.addComponent(p);
    label(c, clk, QStringLiteral("Q"), QStringLiteral("CLK"));
    label(c, id, QStringLiteral("CP1"), QStringLiteral("CLK"));
    Bench bench(c);
    QCOMPARE(bench.sim.componentState(id), 0);
    bench.sim.step(9);
    QCOMPARE(bench.sim.componentState(id), 0);  // no edge yet
    bench.sim.step(3);
    QCOMPARE(bench.sim.componentState(id), 1);  // rising edge at t = 10
    bench.sim.step(20);
    QCOMPARE(bench.sim.componentState(id), 2);
    bench.sim.step(20);
    QCOMPARE(bench.sim.componentState(id), 0);
    QCOMPARE(bench.value(c, id, QStringLiteral("Q0")), Logic::L1);
}

// CircuitMaker's animated devices: a car drives to the finish (F high) and
// back on RESET; a rocket climbs out of sight; a stepper turns half a step
// per change of energized coils, and back.
void TestSimulator::animatedDevices()
{
    Circuit c;
    const ItemId car = place(c, QStringLiteral("car"), {0, 0}, QStringLiteral("CAR1"));
    const ItemId start = place(c, QStringLiteral("logicswitch"), {-300, -100}, QStringLiteral("S1"));
    link(c, start, QStringLiteral("Y"), car, QStringLiteral("S"));
    const ItemId rocket = place(c, QStringLiteral("rocket"), {600, 0}, QStringLiteral("R1"));
    const ItemId fire = place(c, QStringLiteral("logicswitch"), {400, -100}, QStringLiteral("S3"));
    const ItemId gnd = place(c, QStringLiteral("ground"), {400, 200});
    link(c, fire, QStringLiteral("Y"), rocket, QStringLiteral("+"));
    link(c, gnd, QStringLiteral("GND"), rocket, QStringLiteral("-"));
    const ItemId motor = place(c, QStringLiteral("stepper"), {0, 600}, QStringLiteral("M1"));
    const ItemId vcc = place(c, QStringLiteral("vcc"), {300, 400});
    link(c, vcc, QStringLiteral("VCC"), motor, QStringLiteral("COMA"));
    link(c, vcc, QStringLiteral("VCC"), motor, QStringLiteral("COMB"));
    ItemId coil[4];
    for (int k = 0; k < 4; ++k) {
        coil[k] = place(c, QStringLiteral("logicswitch"), {-400, 450.0 + 60 * k}, QStringLiteral("C%1").arg(k + 1));
        link(c, coil[k], QStringLiteral("Y"), motor, QString::number(k + 1));
    }
    Bench bench(c);
    // all coils high, as the commons: nothing energized
    for (ItemId k : coil) bench.sim.interact(k, QStringLiteral("toggle"));
    bench.sim.step(5);
    QCOMPARE(bench.sim.componentState(car), 0);
    QCOMPARE(bench.value(c, car, QStringLiteral("F")), Logic::L0);

    bench.sim.interact(start, QStringLiteral("toggle"));
    bench.sim.interact(fire, QStringLiteral("toggle"));
    bench.sim.step(400);
    QVERIFY(bench.sim.componentState(car) > 0 && bench.sim.componentState(car) < 100);
    QVERIFY(bench.sim.componentState(rocket) > 0);
    bench.sim.step(2000);
    QCOMPARE(bench.sim.componentState(car), 100);
    QCOMPARE(bench.value(c, car, QStringLiteral("F")), Logic::L1);
    QCOMPARE(bench.sim.componentState(rocket), 100);
    bench.sim.interact(start, QStringLiteral("toggle"));
    bench.sim.interact(car, QStringLiteral("reset"));      // its Reset button
    bench.sim.interact(fire, QStringLiteral("toggle"));
    bench.sim.step(5);
    QCOMPARE(bench.sim.componentState(car), 0);
    QCOMPARE(bench.value(c, car, QStringLiteral("F")), Logic::L0);
    QCOMPARE(bench.sim.componentState(rocket), 0);                 // back on its pad

    // full steps 1, 3, 2, 4 (coil pulled low = energized) and back
    auto energize = [&](int k) {
        bench.sim.interact(coil[k - 1], QStringLiteral("toggle"));
        bench.sim.step(3);
    };
    energize(1);
    QCOMPARE(bench.sim.componentState(motor), 0);
    energize(3); energize(1);                                      // 1 off: 3 alone
    QCOMPARE(bench.sim.componentState(motor), 2);                  // 90 degrees (through 45)
    energize(2); energize(3);
    QCOMPARE(bench.sim.componentState(motor), 4);
    energize(4); energize(2);
    QCOMPARE(bench.sim.componentState(motor), 6);
    energize(1); energize(4);
    QCOMPARE(bench.sim.componentState(motor), 8);                  // a full turn
    energize(4); energize(1);
    QCOMPARE(bench.sim.componentState(motor), 6);                  // and back
}

QTEST_GUILESS_MAIN(TestSimulator)
#include "tst_simulator.moc"
