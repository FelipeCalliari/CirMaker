#include "TestUtil.h"

#include "core/Faults.h"
#include "core/io/NativeFormat.h"
#include "sim/analog/SpiceNetlister.h"

using namespace cm;
using namespace cm::test;
using sim::Logic;

namespace {

// Simulation with the faults applied, as the scene runs it.
struct FaultBench {
    std::unique_ptr<Circuit> circuit;
    Netlist netlist;
    sim::EventSimulator sim;

    explicit FaultBench(const Circuit& c) : circuit(faults::copy(c))
    {
        netlist = NetlistBuilder::build(*circuit, library());
        faults::apply(*circuit, netlist, library(), false);
        sim.build(*circuit, library(), netlist);
        sim.reset();
        sim.step(10);
    }
    Logic value(ItemId id, const QString& pin) const
    {
        const SymbolDef* s = library().find(circuit->component(id)->symbolId);
        return sim.netValue(netlist.netOf(id, s->pinIndex(pin)));
    }
};

void tie(Circuit& c, ItemId comp, const QString& pin, bool high)
{
    static int n = 0;
    const ItemId src = place(c, high ? QStringLiteral("vcc") : QStringLiteral("ground"), {-3000.0 - 200 * n++, 3000});
    const QString net = high ? QStringLiteral("HI") : QStringLiteral("LO");
    label(c, src, library().find(c.component(src)->symbolId)->pins[0].name, net);
    label(c, comp, pin, net);
}

void setFaults(Circuit& c, ItemId id, const faults::DeviceFaults& f)
{
    Component comp = *c.component(id);
    faults::write(comp, f);
    c.updateComponent(comp);
}

} // namespace

class TestFaults : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { library(); }
    void propertiesRoundTrip();
    void stuckLowInput();
    void stuckHighOutputForcesNode();
    void openInputFloats();
    void shortedPinsShareNode();
    void analogFaults();
    void settingsSavedWithCircuit();
};

void TestFaults::propertiesRoundTrip()
{
    Component c;
    faults::DeviceFaults f;
    f.enabled = true;
    f.high = {QStringLiteral("A")};
    f.shorted = {QStringLiteral("A"), QStringLiteral("B")};
    f.hint = QStringLiteral("check the inputs");
    faults::write(c, f);
    QCOMPARE(faults::read(c), f);
    QVERIFY(f.active());
    f.replaced = true;
    QVERIFY(!f.active());
    faults::write(c, faults::DeviceFaults{});
    QVERIFY(c.props.isEmpty());
}

void TestFaults::stuckLowInput()
{
    Circuit c;
    const ItemId g = place(c, QStringLiteral("and2"), {0, 0}, QStringLiteral("U1"));
    tie(c, g, QStringLiteral("A"), true);
    tie(c, g, QStringLiteral("B"), true);
    QCOMPARE(FaultBench(c).value(g, QStringLiteral("Y")), Logic::L1);
    faults::DeviceFaults f;
    f.low = {QStringLiteral("A")};
    setFaults(c, g, f);                                 // not enabled yet
    QVERIFY(!faults::anyActive(c));
    QCOMPARE(FaultBench(c).value(g, QStringLiteral("Y")), Logic::L1);
    f.enabled = true;
    setFaults(c, g, f);
    QCOMPARE(FaultBench(c).value(g, QStringLiteral("Y")), Logic::L0);
    f.replaced = true;                                  // the student swapped the chip
    setFaults(c, g, f);
    QCOMPARE(FaultBench(c).value(g, QStringLiteral("Y")), Logic::L1);
}

void TestFaults::stuckHighOutputForcesNode()
{
    Circuit c;
    const ItemId inv = place(c, QStringLiteral("inv"), {0, 0}, QStringLiteral("U1"));
    const ItemId g = place(c, QStringLiteral("and2"), {600, 0}, QStringLiteral("U2"));
    tie(c, inv, QStringLiteral("A"), true);
    tie(c, g, QStringLiteral("B"), true);
    label(c, inv, QStringLiteral("Y"), QStringLiteral("N"));
    label(c, g, QStringLiteral("A"), QStringLiteral("N"));
    QCOMPARE(FaultBench(c).value(g, QStringLiteral("Y")), Logic::L0);
    faults::DeviceFaults f;
    f.enabled = true;
    f.high = {QStringLiteral("Y")};
    setFaults(c, inv, f);
    const FaultBench b(c);
    QCOMPARE(b.value(g, QStringLiteral("A")), Logic::L1);   // the node follows the stuck output
    QCOMPARE(b.value(g, QStringLiteral("Y")), Logic::L1);
}

void TestFaults::openInputFloats()
{
    Circuit c;
    const ItemId g = place(c, QStringLiteral("and2"), {0, 0}, QStringLiteral("U1"));
    tie(c, g, QStringLiteral("A"), true);
    tie(c, g, QStringLiteral("B"), true);
    faults::DeviceFaults f;
    f.enabled = true;
    f.open = {QStringLiteral("A")};
    setFaults(c, g, f);
    const FaultBench b(c);
    const SymbolDef* s = library().find(QStringLiteral("and2"));
    const Logic floating = s->pins[s->pinIndex(QStringLiteral("A"))].floatValue ? Logic::L1 : Logic::L0;
    QCOMPARE(b.value(g, QStringLiteral("Y")), floating);
    // the rest of the node keeps its level
    QCOMPARE(b.value(g, QStringLiteral("B")), Logic::L1);
}

void TestFaults::shortedPinsShareNode()
{
    Circuit c;
    const ItemId g = place(c, QStringLiteral("or2"), {0, 0}, QStringLiteral("U1"));
    tie(c, g, QStringLiteral("A"), false);
    label(c, g, QStringLiteral("B"), QStringLiteral("FREE"));
    const ItemId sw = place(c, QStringLiteral("logicswitch"), {-600, 600}, QStringLiteral("S1"));
    label(c, sw, QStringLiteral("Y"), QStringLiteral("FREE"));
    faults::DeviceFaults f;
    f.enabled = true;
    f.shorted = {QStringLiteral("B"), QStringLiteral("Y")};
    setFaults(c, g, f);
    const FaultBench b(c);
    const int nb = b.netlist.netOf(g, library().find(QStringLiteral("or2"))->pinIndex(QStringLiteral("B")));
    const int ny = b.netlist.netOf(g, library().find(QStringLiteral("or2"))->pinIndex(QStringLiteral("Y")));
    QCOMPARE(nb, ny);
}

void TestFaults::analogFaults()
{
    Circuit c;
    const ItemId r = place(c, QStringLiteral("resistor"), {0, 0}, QStringLiteral("R1"), QStringLiteral("1k"));
    const ItemId v = place(c, QStringLiteral("vcc"), {-600, 0}, QStringLiteral("V1"));
    const ItemId gnd = place(c, QStringLiteral("ground"), {600, 600});
    const SymbolDef* rs = library().find(QStringLiteral("resistor"));
    label(c, v, library().find(QStringLiteral("vcc"))->pins[0].name, QStringLiteral("TOP"));
    label(c, r, rs->pins[0].name, QStringLiteral("TOP"));
    label(c, r, rs->pins[1].name, QStringLiteral("0"));
    label(c, gnd, library().find(QStringLiteral("ground"))->pins[0].name, QStringLiteral("0"));
    faults::DeviceFaults f;
    f.enabled = true;
    f.value = QStringLiteral("47k");
    f.open = {rs->pins[0].name};
    setFaults(c, r, f);
    auto circuit = faults::copy(c);
    Netlist n = NetlistBuilder::build(*circuit, library());
    faults::apply(*circuit, n, library(), true);
    const QString deck = spice::build(*circuit, library(), n).text;
    QVERIFY2(deck.contains(QStringLiteral(" 47k")), qPrintable(deck));     // wrong value
    QVERIFY2(deck.contains(QStringLiteral(" 1G")), qPrintable(deck));      // open: series resistor
}

void TestFaults::settingsSavedWithCircuit()
{
    Circuit c;
    FaultSettings s;
    s.password = faults::hashPassword(QStringLiteral("prof"));
    s.disabled = {QStringLiteral("delete"), QStringLiteral("trace")};
    s.open = QStringLiteral("10Meg");
    c.setFaultSettings(s);
    QVERIFY(s.locked());
    QVERIFY(s.disables(QStringLiteral("trace")));
    QVERIFY(!s.disables(QStringLiteral("wire")));
    QVERIFY(faults::hashPassword(QStringLiteral("prof")) != faults::hashPassword(QStringLiteral("Prof")));
    Circuit loaded;
    QVERIFY(NativeFormat::deserialize(NativeFormat::serialize(c), loaded, nullptr));
    QCOMPARE(loaded.faultSettings(), s);
}

QTEST_GUILESS_MAIN(TestFaults)
#include "tst_faults.moc"
