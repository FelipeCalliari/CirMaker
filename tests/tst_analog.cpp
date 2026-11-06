#include "TestUtil.h"
#include "core/io/CmModels.h"

#include "sim/analog/NgspiceEngine.h"
#include "sim/analog/SpiceNetlister.h"
#include "core/io/SpiceImporter.h"
#include "core/io/CktImporter.h"

#include <QDirIterator>
#include <QElapsedTimer>

using namespace cm;
using namespace cm::test;

class TestAnalog : public QObject {
    Q_OBJECT
private slots:
    void parsesSpiceNumbers();
    void deckNamesGroundAndLabels();
    void dividerOperatingPoint();
    void diodeForwardDrop();
    void rcTransient();
    void lowPassAc();
    void invertingAmplifier();
    void mixedModeInverter();
    void dataSequencerPwl();
    void generatorWaves_data();
    void generatorWaves();
    void fourierMath();
    void fourierOfTransient();
    void transferFunction();
    void noiseSpectrum();
    void sweeps();
    void timer555Astable();
    void relayAnalog();
    void transformerRatio();
    void optoIsolator();
    void scrLatches();
    void triacBothWays();
    void deviceProbeCurrents();
    void exportsSubcircuit();
    void regulatorAndBridge();
    void crystalResonance();
    void miscPartsSimulate_data();
    void miscPartsSimulate();
    void importsSpiceNetlist();
    void labelledModels();
    void controlledSources_data();
    void controlledSources();
    void converters();
    void blocksAndExpressions();
    void mathBlocks();
    void vcoAndPll();
    void cmDevices();
    void transmissionLineDelay();
    void unijunctions();
    void ownSpiceLine();
    void mixedModeCounter();
    void mixedModeRam();
    void mixedModeTristate();
    void frequencyToVoltage();
    void balancedModulator();
    void gateOscillatorFromInitialConditions();
    void importedExamplesSimulate();

private:
    static spice::Result simulate(const Circuit& c, spice::Deck* deck = nullptr);
    static void requireEngine();
};

namespace {

ItemId part(Circuit& c, const QString& symbol, const QString& ref, const QString& value = {},
            const QVariantMap& props = {})
{
    Component comp;
    comp.symbolId = symbol;
    comp.designator = ref;
    comp.label = value;
    comp.props = props;
    comp.pos = {double(c.components().size()) * 400, 0};
    return c.addComponent(comp);
}

void ground(Circuit& c, const QString& net)
{
    const ItemId g = part(c, QStringLiteral("ground"), QStringLiteral("GND%1").arg(c.components().size()));
    label(c, g, QStringLiteral("GND"), net);
}

double nodeVoltage(const spice::Result& r, const spice::Deck& deck, const Circuit& c, ItemId comp, const QString& pin)
{
    const SymbolDef* s = library().find(c.component(comp)->symbolId);
    const Netlist nl = NetlistBuilder::build(c, library());
    const QString node = deck.nodeOfNet.value(nl.netOf(comp, s->pinIndex(pin)));
    return node == u"0" ? 0.0 : r.last(node).value_or(qQNaN());
}

} // namespace

void TestAnalog::requireEngine()
{
    QString why;
    if (!spice::NgspiceEngine::instance().available(&why))
        QSKIP(qPrintable(why));
}

spice::Result TestAnalog::simulate(const Circuit& c, spice::Deck* deckOut)
{
    const Netlist nl = NetlistBuilder::build(c, library());
    const spice::Deck deck = spice::build(c, library(), nl);
    if (deckOut) *deckOut = deck;
    if (!deck.ok) {
        spice::Result r;
        r.error = deck.warnings.join(u"; ");
        return r;
    }
    spice::Result r = spice::NgspiceEngine::instance().run(deck.text);
    if (!r.ok)
        qWarning("%s\n---\n%s", qPrintable(deck.text), qPrintable(r.log));
    return r;
}

void TestAnalog::parsesSpiceNumbers()
{
    QCOMPARE(spice::parseNumber(QStringLiteral("4.7k")).value(), 4700.0);
    QCOMPARE(spice::parseNumber(QStringLiteral("10uF")).value(), 10e-6);
    QCOMPARE(spice::parseNumber(QStringLiteral("1Meg")).value(), 1e6);
    QCOMPARE(spice::parseNumber(QStringLiteral("2m")).value(), 2e-3);
    QCOMPARE(spice::parseNumber(QStringLiteral("5V")).value(), 5.0);
    QVERIFY(!spice::parseNumber(QStringLiteral("abc")));
}

void TestAnalog::deckNamesGroundAndLabels()
{
    Circuit c;
    const ItemId r = part(c, QStringLiteral("resistor"), QStringLiteral("R1"), QStringLiteral("1k"));
    label(c, r, QStringLiteral("1"), QStringLiteral("OUT"));
    ground(c, QStringLiteral("G"));
    label(c, r, QStringLiteral("2"), QStringLiteral("G"));
    c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
    const spice::Deck deck = spice::build(c, library(), NetlistBuilder::build(c, library()));
    QVERIFY2(deck.text.contains(QStringLiteral("Rr1 out 0 1k")), qPrintable(deck.text));
    QVERIFY(deck.text.contains(QStringLiteral(".op")));
}

void TestAnalog::dividerOperatingPoint()
{
    requireEngine();
    Circuit c;
    const ItemId v = part(c, QStringLiteral("vsource"), QStringLiteral("V1"), QStringLiteral("10V"));
    const ItemId r1 = part(c, QStringLiteral("resistor"), QStringLiteral("R1"), QStringLiteral("1k"));
    const ItemId r2 = part(c, QStringLiteral("resistor"), QStringLiteral("R2"), QStringLiteral("3k"));
    label(c, v, QStringLiteral("+"), QStringLiteral("IN"));
    label(c, r1, QStringLiteral("1"), QStringLiteral("IN"));
    label(c, r1, QStringLiteral("2"), QStringLiteral("MID"));
    label(c, r2, QStringLiteral("1"), QStringLiteral("MID"));
    label(c, r2, QStringLiteral("2"), QStringLiteral("G"));
    label(c, v, QStringLiteral("-"), QStringLiteral("G"));
    ground(c, QStringLiteral("G"));
    c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(qAbs(nodeVoltage(r, deck, c, r2, QStringLiteral("1")) - 7.5) < 1e-6);
}

void TestAnalog::diodeForwardDrop()
{
    requireEngine();
    Circuit c;
    const ItemId v = part(c, QStringLiteral("vsource"), QStringLiteral("V1"), QStringLiteral("5"));
    const ItemId r = part(c, QStringLiteral("resistor"), QStringLiteral("R1"), QStringLiteral("1k"));
    const ItemId d = part(c, QStringLiteral("diode"), QStringLiteral("D1"));
    label(c, v, QStringLiteral("+"), QStringLiteral("IN"));
    label(c, r, QStringLiteral("1"), QStringLiteral("IN"));
    label(c, r, QStringLiteral("2"), QStringLiteral("A"));
    label(c, d, QStringLiteral("A"), QStringLiteral("A"));
    label(c, d, QStringLiteral("K"), QStringLiteral("G"));
    label(c, v, QStringLiteral("-"), QStringLiteral("G"));
    ground(c, QStringLiteral("G"));
    c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
    spice::Deck deck;
    const spice::Result res = simulate(c, &deck);
    QVERIFY2(res.ok, qPrintable(res.error));
    const double vd = nodeVoltage(res, deck, c, d, QStringLiteral("A"));
    QVERIFY2(vd > 0.55 && vd < 0.8, qPrintable(QString::number(vd)));
}

void TestAnalog::rcTransient()
{
    requireEngine();
    Circuit c;
    // 0 -> 5 V square wave, slow enough to be a step for the first 5 ms
    const ItemId g = part(c, QStringLiteral("siggen"), QStringLiteral("V1"), {},
                          {{QStringLiteral("wave"), QStringLiteral("square")}, {QStringLiteral("amplitude"), QStringLiteral("2.5")},
                           {QStringLiteral("offset"), QStringLiteral("2.5")}, {QStringLiteral("frequency"), QStringLiteral("100")}});
    const ItemId r = part(c, QStringLiteral("resistor"), QStringLiteral("R1"), QStringLiteral("1k"));
    const ItemId cap = part(c, QStringLiteral("capacitor"), QStringLiteral("C1"), QStringLiteral("1u"));
    label(c, g, QStringLiteral("+"), QStringLiteral("IN"));
    label(c, r, QStringLiteral("1"), QStringLiteral("IN"));
    label(c, r, QStringLiteral("2"), QStringLiteral("OUT"));
    label(c, cap, QStringLiteral("1"), QStringLiteral("OUT"));
    label(c, cap, QStringLiteral("2"), QStringLiteral("G"));
    label(c, g, QStringLiteral("-"), QStringLiteral("G"));
    ground(c, QStringLiteral("G"));
    AnalysisSettings a{true, AnalysisSettings::Type::Transient};
    a.tranStop = QStringLiteral("1m");
    a.tranStep = QStringLiteral("1u");
    c.setAnalysis(a);
    const spice::Result res = simulate(c);
    QVERIFY2(res.ok, qPrintable(res.error));
    QCOMPARE(res.scale, QStringLiteral("time"));
    const double v = res.last(QStringLiteral("out")).value();      // at t = tau
    QVERIFY2(qAbs(v - 5 * (1 - std::exp(-1.0))) < 0.05, qPrintable(QString::number(v)));
}

void TestAnalog::lowPassAc()
{
    requireEngine();
    Circuit c;
    const ItemId g = part(c, QStringLiteral("siggen"), QStringLiteral("V1"));
    const ItemId r = part(c, QStringLiteral("resistor"), QStringLiteral("R1"), QStringLiteral("1k"));
    const ItemId cap = part(c, QStringLiteral("capacitor"), QStringLiteral("C1"), QStringLiteral("159.155n"));
    label(c, g, QStringLiteral("+"), QStringLiteral("IN"));
    label(c, r, QStringLiteral("1"), QStringLiteral("IN"));
    label(c, r, QStringLiteral("2"), QStringLiteral("OUT"));
    label(c, cap, QStringLiteral("1"), QStringLiteral("OUT"));
    label(c, cap, QStringLiteral("2"), QStringLiteral("G"));
    label(c, g, QStringLiteral("-"), QStringLiteral("G"));
    ground(c, QStringLiteral("G"));
    AnalysisSettings a{true, AnalysisSettings::Type::AC};
    a.acStart = QStringLiteral("100");
    a.acStop = QStringLiteral("1k");                    // last point is the corner frequency
    c.setAnalysis(a);
    const spice::Result res = simulate(c);
    QVERIFY2(res.ok, qPrintable(res.error));
    const spice::Vector* out = res.find(QStringLiteral("out"));
    QVERIFY(out && out->isComplex());
    QVERIFY2(qAbs(std::abs(out->complex.last()) - M_SQRT1_2) < 0.01, qPrintable(QString::number(std::abs(out->complex.last()))));
}

void TestAnalog::invertingAmplifier()
{
    requireEngine();
    Circuit c;
    const ItemId vin = part(c, QStringLiteral("vsource"), QStringLiteral("VIN"), QStringLiteral("0.1"));
    const ItemId vp = part(c, QStringLiteral("vsource"), QStringLiteral("VP"), QStringLiteral("15"));
    const ItemId vn = part(c, QStringLiteral("vsource"), QStringLiteral("VN"), QStringLiteral("15"));
    const ItemId ri = part(c, QStringLiteral("resistor"), QStringLiteral("R1"), QStringLiteral("1k"));
    const ItemId rf = part(c, QStringLiteral("resistor"), QStringLiteral("R2"), QStringLiteral("10k"));
    const ItemId u = part(c, QStringLiteral("opamp"), QStringLiteral("U1"));
    auto l = [&](ItemId id, const char* pin, const char* net) { label(c, id, QString::fromLatin1(pin), QString::fromLatin1(net)); };
    l(vin, "+", "IN"); l(vin, "-", "G");
    l(vp, "+", "VCC"); l(vp, "-", "G");
    l(vn, "+", "G"); l(vn, "-", "VEE");
    l(ri, "1", "IN"); l(ri, "2", "SUM");
    l(rf, "1", "SUM"); l(rf, "2", "OUT");
    l(u, "-", "SUM"); l(u, "+", "G"); l(u, "OUT", "OUT"); l(u, "V+", "VCC"); l(u, "V-", "VEE");
    ground(c, QStringLiteral("G"));
    c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
    const spice::Result res = simulate(c);
    QVERIFY2(res.ok, qPrintable(res.error));
    const double out = res.last(QStringLiteral("out")).value();
    QVERIFY2(qAbs(out + 1.0) < 0.01, qPrintable(QString::number(out)));

    // labelled UA741: CircuitMaker's own subcircuit, nodes in its pin order
    if (cmmodels::findSubckt(QStringLiteral("UA741")).lines.isEmpty()) return;
    Component op = *c.component(u);
    op.label = QStringLiteral("UA741");
    c.updateComponent(op);
    spice::Deck deck;
    const spice::Result res741 = simulate(c, &deck);
    QVERIFY2(deck.text.contains(QStringLiteral("CM_UA741")), qPrintable(deck.text));
    QVERIFY2(res741.ok, qPrintable(res741.error));
    const double out741 = res741.last(QStringLiteral("out")).value();
    QVERIFY2(qAbs(out741 + 1.0) < 0.02, qPrintable(QString::number(out741)));
}

void TestAnalog::mixedModeInverter()
{
    requireEngine();
    Circuit c;
    const ItemId p = part(c, QStringLiteral("pulser"), QStringLiteral("P1"), {},
                          {{QStringLiteral("high"), 10}, {QStringLiteral("low"), 10}});
    const ItemId inv = part(c, QStringLiteral("7404"), QStringLiteral("U1"));
    const ItemId r = part(c, QStringLiteral("resistor"), QStringLiteral("R1"), QStringLiteral("10k"));
    label(c, p, QStringLiteral("Q"), QStringLiteral("CLK"));
    label(c, inv, QStringLiteral("A"), QStringLiteral("CLK"));
    label(c, inv, QStringLiteral("Y"), QStringLiteral("NCLK"));
    label(c, r, QStringLiteral("1"), QStringLiteral("NCLK"));
    label(c, r, QStringLiteral("2"), QStringLiteral("G"));
    ground(c, QStringLiteral("G"));
    AnalysisSettings a{true, AnalysisSettings::Type::Transient};
    a.tranStop = QStringLiteral("40u");
    a.tranStep = QStringLiteral("0.1u");
    c.setAnalysis(a);
    const spice::Result res = simulate(c);
    QVERIFY2(res.ok, qPrintable(res.error));
    const spice::Vector* t = res.find(QStringLiteral("time"));
    const spice::Vector* in = res.find(QStringLiteral("clk"));
    const spice::Vector* out = res.find(QStringLiteral("nclk"));
    QVERIFY(t && in && out);
    // sample in the middle of the low (0-10 us) and high (10-20 us) phases
    auto at = [&](const spice::Vector* v, double time) {
        for (int i = 0; i < t->size(); ++i)
            if (t->real[i] >= time) return v->real[i];
        return v->real.last();
    };
    QVERIFY(at(in, 5e-6) < 0.5 && at(out, 5e-6) > 4.5);
    QVERIFY(at(in, 15e-6) > 4.5 && at(out, 15e-6) < 0.5);
}

// Every CircuitMaker example that imports into analog mode must at least
// produce a deck ngspice accepts, unless it uses parts without an equivalent.
// Words 01 02 03, 1 ms each, repeating: at 4.5 ms the word is 02 again.
void TestAnalog::dataSequencerPwl()
{
    requireEngine();
    Circuit c;
    const ItemId ds = part(c, QStringLiteral("dataseq"), QStringLiteral("DS1"), {},
                           {{QStringLiteral("data"), QStringLiteral("01 02 03")}, {QStringLiteral("lines"), 3},
                            {QStringLiteral("stop"), 2}, {QStringLiteral("step"), QStringLiteral("1m")}});
    const ItemId r0 = part(c, QStringLiteral("resistor"), QStringLiteral("R1"), QStringLiteral("1k"));
    const ItemId r1 = part(c, QStringLiteral("resistor"), QStringLiteral("R2"), QStringLiteral("1k"));
    label(c, ds, QStringLiteral("Q0"), QStringLiteral("B0"));
    label(c, ds, QStringLiteral("Q1"), QStringLiteral("B1"));
    label(c, r0, QStringLiteral("1"), QStringLiteral("B0"));
    label(c, r1, QStringLiteral("1"), QStringLiteral("B1"));
    label(c, r0, QStringLiteral("2"), QStringLiteral("G"));
    label(c, r1, QStringLiteral("2"), QStringLiteral("G"));
    ground(c, QStringLiteral("G"));
    AnalysisSettings a{true, AnalysisSettings::Type::Transient};
    a.tranStop = QStringLiteral("4.5m");
    a.tranStep = QStringLiteral("10u");
    c.setAnalysis(a);
    spice::Deck deck;
    const spice::Result res = simulate(c, &deck);
    QVERIFY2(res.ok, qPrintable(res.error + u'\n' + deck.text));
    QVERIFY(qAbs(nodeVoltage(res, deck, c, ds, QStringLiteral("Q0")) - 0) < 0.01);
    QVERIFY(qAbs(nodeVoltage(res, deck, c, ds, QStringLiteral("Q1")) - 5) < 0.01);
}

void TestAnalog::generatorWaves_data()
{
    QTest::addColumn<QVariantMap>("props");
    QTest::addColumn<QString>("stop");
    QTest::addColumn<double>("expected");       // NaN: only has to simulate
    QTest::addColumn<QString>("deckHas");
    const double any = std::numeric_limits<double>::quiet_NaN();
    QTest::newRow("sine delayed") << QVariantMap{{"wave", "sine"}, {"amplitude", "2"}, {"frequency", "1k"}, {"delay", "1m"}}
                                  << "0.9m" << 0.0 << "SIN(0 2 1000 0.001 0)";
    QTest::newRow("pulse") << QVariantMap{{"wave", "pulse"}, {"v1", "0"}, {"v2", "3"}, {"period", "1m"}, {"width", "0.5m"}}
                           << "1.25m" << 3.0 << "PULSE(0 3 0";
    QTest::newRow("exp") << QVariantMap{{"wave", "exp"}, {"v1", "0"}, {"v2", "5"}, {"rt", "0.1m"}, {"fd", "1"}}
                         << "2m" << 5.0 << "EXP(0 5";
    QTest::newRow("pwl") << QVariantMap{{"wave", "pwl"}, {"pwl", "0 0\n1m 2\n2m 4"}} << "2m" << 4.0 << "PWL(0 0 0.001 2 0.002 4)";
    QTest::newRow("am") << QVariantMap{{"wave", "am"}, {"carrier", "10k"}, {"signal", "1k"}, {"mdi", "0.5"}} << "1m" << any
                        << "sin(2*pi*1000*time)";
    QTest::newRow("fm") << QVariantMap{{"wave", "fm"}, {"carrier", "10k"}, {"signal", "1k"}, {"mdi", "2"}} << "1m" << any
                        << "SFFM(0 1 10000 2 1000)";
    QTest::newRow("current") << QVariantMap{{"wave", "sine"}, {"current", true}, {"amplitude", "1m"}} << "1m" << any << "Iv1 out 0 DC 0 SIN(";
}

void TestAnalog::generatorWaves()
{
    QFETCH(QVariantMap, props);
    QFETCH(QString, stop);
    QFETCH(double, expected);
    QFETCH(QString, deckHas);
    Circuit c;
    QVariantMap p = props;
    if (p.contains("pwl")) p["pwl"] = p["pwl"].toString().replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    const ItemId g = part(c, QStringLiteral("siggen"), QStringLiteral("V1"), {}, p);
    const ItemId r = part(c, QStringLiteral("resistor"), QStringLiteral("R1"), QStringLiteral("1k"));
    label(c, g, QStringLiteral("+"), QStringLiteral("OUT"));
    label(c, r, QStringLiteral("1"), QStringLiteral("OUT"));
    label(c, r, QStringLiteral("2"), QStringLiteral("G"));
    label(c, g, QStringLiteral("-"), QStringLiteral("G"));
    ground(c, QStringLiteral("G"));
    AnalysisSettings a{true, AnalysisSettings::Type::Transient};
    a.tranStop = stop;
    a.tranStep = QStringLiteral("5u");
    c.setAnalysis(a);
    const Netlist n = NetlistBuilder::build(c, library());
    const spice::Deck deck = spice::build(c, library(), n);
    QVERIFY2(deck.text.contains(deckHas), qPrintable(deck.text));
    requireEngine();
    const spice::Result res = simulate(c);
    QVERIFY2(res.ok, qPrintable(res.error + u'\n' + deck.text));
    const double v = res.last(QStringLiteral("out")).value_or(std::numeric_limits<double>::quiet_NaN());
    QVERIFY(std::isfinite(v));
    if (!std::isnan(expected))
        QVERIFY2(qAbs(v - expected) < 0.05, qPrintable(QString::number(v)));
}

namespace {

// V1 (vsource) - R1 - OUT - R2 - ground
Circuit* divider(Circuit& c, const QString& r1 = QStringLiteral("1k"))
{
    const ItemId v = part(c, QStringLiteral("vsource"), QStringLiteral("V1"), QStringLiteral("10"));
    const ItemId a = part(c, QStringLiteral("resistor"), QStringLiteral("R1"), r1);
    const ItemId b = part(c, QStringLiteral("resistor"), QStringLiteral("R2"), QStringLiteral("1k"));
    label(c, v, QStringLiteral("+"), QStringLiteral("IN"));
    label(c, a, QStringLiteral("1"), QStringLiteral("IN"));
    label(c, a, QStringLiteral("2"), QStringLiteral("OUT"));
    label(c, b, QStringLiteral("1"), QStringLiteral("OUT"));
    label(c, b, QStringLiteral("2"), QStringLiteral("G"));
    label(c, v, QStringLiteral("-"), QStringLiteral("G"));
    ground(c, QStringLiteral("G"));
    return &c;
}

} // namespace

void TestAnalog::fourierMath()
{
    QVector<double> t, sine, square;
    for (int i = 0; i <= 4000; ++i) {
        const double x = i * 2e-3 / 4000;                    // two periods of 1 kHz
        t << x;
        sine << 0.5 + 2 * std::sin(2 * M_PI * 1e3 * x);
        square << (std::fmod(x * 1e3, 1.0) < 0.5 ? 1.0 : -1.0);
    }
    const spice::Fourier f = spice::fourier(t, sine, 1e3, 9);
    QCOMPARE(f.magnitude.size(), 10);
    QVERIFY(qAbs(f.magnitude[0] - 0.5) < 1e-3);
    QVERIFY(qAbs(f.magnitude[1] - 2) < 1e-3);
    QVERIFY(qAbs(f.phase[1]) < 1);
    QVERIFY(f.thd < 0.1);
    const spice::Fourier q = spice::fourier(t, square, 1e3, 9);
    QVERIFY(qAbs(q.magnitude[1] - 4 / M_PI) < 0.01);         // 4/pi, odd harmonics 1/n
    QVERIFY(qAbs(q.magnitude[3] - 4 / (3 * M_PI)) < 0.01);
    QVERIFY(q.magnitude[2] < 0.01);
    QVERIFY(q.thd > 35 && q.thd < 45);                       // 9 harmonics of a square: ~42 %
    QVERIFY(spice::fourier(t, sine, 100, 5).magnitude.isEmpty());   // shorter than a period
}

void TestAnalog::fourierOfTransient()
{
    requireEngine();
    Circuit c;
    const ItemId g = part(c, QStringLiteral("siggen"), QStringLiteral("V1"), {},
                          {{QStringLiteral("amplitude"), QStringLiteral("3")}, {QStringLiteral("frequency"), QStringLiteral("1k")}});
    const ItemId r = part(c, QStringLiteral("resistor"), QStringLiteral("R1"), QStringLiteral("1k"));
    label(c, g, QStringLiteral("+"), QStringLiteral("OUT"));
    label(c, r, QStringLiteral("1"), QStringLiteral("OUT"));
    label(c, r, QStringLiteral("2"), QStringLiteral("G"));
    label(c, g, QStringLiteral("-"), QStringLiteral("G"));
    ground(c, QStringLiteral("G"));
    AnalysisSettings a{true, AnalysisSettings::Type::Fourier};
    a.tranStop = QStringLiteral("3m");
    a.tranStep = QStringLiteral("2u");
    c.setAnalysis(a);
    spice::Deck deck;
    const spice::Result res = simulate(c, &deck);
    QVERIFY2(res.ok, qPrintable(res.error));
    QCOMPARE(deck.generatorFrequency, 1000.0);
    QVERIFY(deck.text.contains(QStringLiteral(".tran 2u 3m")));
    const spice::Fourier f = spice::fourier(res.find(QStringLiteral("time"))->real, res.find(QStringLiteral("out"))->real,
                                            deck.generatorFrequency, 5);
    QVERIFY2(qAbs(f.magnitude[1] - 3) < 0.02, qPrintable(QString::number(f.magnitude[1])));
    QVERIFY(f.thd < 1);
}

void TestAnalog::transferFunction()
{
    requireEngine();
    Circuit c;
    divider(c);
    AnalysisSettings a{true, AnalysisSettings::Type::TransferFunction};
    a.output = QStringLiteral("OUT");
    a.inputSource = QStringLiteral("V1");
    c.setAnalysis(a);
    spice::Deck deck;
    const spice::Result res = simulate(c, &deck);
    QVERIFY2(res.ok, qPrintable(res.error + u'\n' + deck.text));
    QCOMPARE(res.plotType, QStringLiteral("tf"));
    double tf = 0, zin = 0, zout = 0;
    for (auto it = res.vectors.cbegin(); it != res.vectors.cend(); ++it) {
        if (it.key().contains(u"transfer")) tf = it->real.value(0);
        if (it.key().contains(u"input_impedance")) zin = it->real.value(0);
        if (it.key().contains(u"output_impedance")) zout = it->real.value(0);
    }
    QVERIFY(qAbs(tf - 0.5) < 1e-6);
    QVERIFY(qAbs(zin - 2000) < 1e-3);
    QVERIFY(qAbs(zout - 500) < 1e-3);

    a.output = QStringLiteral("NOWHERE");
    c.setAnalysis(a);
    QVERIFY(!spice::build(c, library(), NetlistBuilder::build(c, library())).ok);
}

void TestAnalog::noiseSpectrum()
{
    requireEngine();
    Circuit c;
    divider(c);
    AnalysisSettings a{true, AnalysisSettings::Type::Noise};
    a.output = QStringLiteral("out");                  // net names are case-insensitive
    a.inputSource = QStringLiteral("V1");
    a.acStart = QStringLiteral("10");
    a.acStop = QStringLiteral("10k");
    a.acPointsPerDecade = 5;
    c.setAnalysis(a);
    spice::Deck deck;
    const spice::Result res = simulate(c, &deck);
    QVERIFY2(res.ok, qPrintable(res.error + u'\n' + deck.text));
    QCOMPARE(res.scale, QStringLiteral("frequency"));
    const spice::Vector* onoise = res.find(QStringLiteral("onoise_spectrum"));
    QVERIFY(onoise && onoise->size() > 10);
    // two 1k resistors in parallel at 27 C: sqrt(4kTR) = 2.88 nV/sqrt(Hz)
    QVERIFY2(qAbs(onoise->real.value(0) - 2.88e-9) < 0.1e-9, qPrintable(QString::number(onoise->real.value(0))));
}

void TestAnalog::sweeps()
{
    Circuit c;
    divider(c);
    AnalysisSettings a{true, AnalysisSettings::Type::OperatingPoint};
    a.sweep = AnalysisSettings::Sweep::Temperature;
    a.tempStart = QStringLiteral("0");
    a.tempStop = QStringLiteral("100");
    a.tempStep = QStringLiteral("25");
    c.setAnalysis(a);
    const Netlist nl = NetlistBuilder::build(c, library());
    QString error;
    QVector<spice::Run> runs = spice::buildRuns(c, library(), nl, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(runs.size(), 5);
    QVERIFY(runs[2].deck.text.contains(QStringLiteral(".temp 50\n.op")));

    a.sweep = AnalysisSettings::Sweep::Parameter;
    a.paramDevice = QStringLiteral("R1");
    a.paramStart = QStringLiteral("1k");
    a.paramStop = QStringLiteral("3k");
    a.paramStep = QStringLiteral("1k");
    c.setAnalysis(a);
    runs = spice::buildRuns(c, library(), nl, &error);
    QCOMPARE(runs.size(), 3);
    QCOMPARE(runs[1].label, QStringLiteral("R1 = 2000"));
    QVERIFY(runs[1].deck.text.contains(QStringLiteral(" 2000\n")));

    a.sweep = AnalysisSettings::Sweep::MonteCarlo;
    a.mcRuns = 4;
    a.mcTolerance = QStringLiteral("20");
    c.setAnalysis(a);
    runs = spice::buildRuns(c, library(), nl, &error);
    QCOMPARE(runs.size(), 4);
    QVERIFY(runs[0].deck.text.contains(QStringLiteral(" 1k\n")) || runs[0].deck.text.contains(QStringLiteral(" 1000\n")));
    QVERIFY(runs[1].deck.text != runs[2].deck.text);

    a.paramStep = QStringLiteral("-1k");               // runs away from the stop value
    a.sweep = AnalysisSettings::Sweep::Parameter;
    c.setAnalysis(a);
    runs = spice::buildRuns(c, library(), nl, &error);
    QVERIFY(runs.isEmpty() && !error.isEmpty());

    // the parameter sweep really changes the result
    requireEngine();
    a.paramStep = QStringLiteral("2k");
    c.setAnalysis(a);
    error.clear();
    runs = spice::buildRuns(c, library(), nl, &error);
    QCOMPARE(runs.size(), 2);
    const double v1 = spice::NgspiceEngine::instance().run(runs[0].deck.text).last(QStringLiteral("out")).value_or(0);
    const double v3 = spice::NgspiceEngine::instance().run(runs[1].deck.text).last(QStringLiteral("out")).value_or(0);
    QVERIFY(qAbs(v1 - 5) < 1e-6);
    QVERIFY(qAbs(v3 - 2.5) < 1e-6);
}

namespace {

void wire(Circuit& c, ItemId comp, const QString& pin, const QString& net) { label(c, comp, pin, net); }

ItemId supply(Circuit& c, const QString& net, const QString& volts)
{
    const ItemId v = part(c, QStringLiteral("vsource"), QStringLiteral("VS%1").arg(c.components().size()), volts);
    wire(c, v, QStringLiteral("+"), net);
    wire(c, v, QStringLiteral("-"), QStringLiteral("G"));
    return v;
}

ItemId resistor(Circuit& c, const QString& value, const QString& a, const QString& b)
{
    const ItemId r = part(c, QStringLiteral("resistor"), QStringLiteral("R%1").arg(c.components().size()), value);
    wire(c, r, QStringLiteral("1"), a);
    wire(c, r, QStringLiteral("2"), b);
    return r;
}

void transient(Circuit& c, const QString& stop, const QString& step)
{
    AnalysisSettings a{true, AnalysisSettings::Type::Transient};
    a.tranStop = stop;
    a.tranStep = step;
    c.setAnalysis(a);
}

// times at which v rises through the threshold
QVector<double> risingEdges(const spice::Result& r, const QString& node, double threshold)
{
    const spice::Vector* t = r.find(QStringLiteral("time"));
    const spice::Vector* v = r.find(node);
    QVector<double> out;
    for (int i = 1; t && v && i < v->size(); ++i)
        if (v->real[i - 1] < threshold && v->real[i] >= threshold) out << t->real[i];
    return out;
}

} // namespace

void TestAnalog::timer555Astable()
{
    requireEngine();
    Circuit c;
    const ItemId u = part(c, QStringLiteral("555"), QStringLiteral("U1"));
    supply(c, QStringLiteral("VCC"), QStringLiteral("5"));
    ground(c, QStringLiteral("G"));
    wire(c, u, QStringLiteral("VCC"), QStringLiteral("VCC"));
    wire(c, u, QStringLiteral("~RST"), QStringLiteral("VCC"));
    wire(c, u, QStringLiteral("GND"), QStringLiteral("G"));
    wire(c, u, QStringLiteral("DIS"), QStringLiteral("DIS"));
    wire(c, u, QStringLiteral("THR"), QStringLiteral("TC"));
    wire(c, u, QStringLiteral("TRG"), QStringLiteral("TC"));
    wire(c, u, QStringLiteral("OUT"), QStringLiteral("OUT"));
    resistor(c, QStringLiteral("1k"), QStringLiteral("VCC"), QStringLiteral("DIS"));
    resistor(c, QStringLiteral("10k"), QStringLiteral("DIS"), QStringLiteral("TC"));
    const ItemId cap = part(c, QStringLiteral("capacitor"), QStringLiteral("C1"), QStringLiteral("10n"));
    wire(c, cap, QStringLiteral("1"), QStringLiteral("TC"));
    wire(c, cap, QStringLiteral("2"), QStringLiteral("G"));
    resistor(c, QStringLiteral("10k"), QStringLiteral("OUT"), QStringLiteral("G"));
    transient(c, QStringLiteral("2m"), QStringLiteral("1u"));
    spice::Deck deck;
    const spice::Result res = simulate(c, &deck);
    QVERIFY2(res.ok, qPrintable(res.error + u'\n' + deck.text));
    const QVector<double> edges = risingEdges(res, QStringLiteral("out"), 2.5);
    QVERIFY2(edges.size() >= 8, qPrintable(QString::number(edges.size())));
    // f = 1.44 / ((RA + 2 RB) C) = 6.86 kHz
    const double period = (edges.last() - edges[2]) / (edges.size() - 3);
    QVERIFY2(qAbs(1 / period - 6857) < 700, qPrintable(QString::number(1 / period)));
}

void TestAnalog::relayAnalog()
{
    requireEngine();
    for (const bool energized : {false, true}) {
        Circuit c;
        const ItemId k = part(c, QStringLiteral("relay_spdt"), QStringLiteral("K1"));
        supply(c, QStringLiteral("COIL"), energized ? QStringLiteral("5") : QStringLiteral("0"));
        supply(c, QStringLiteral("COM"), QStringLiteral("10"));
        ground(c, QStringLiteral("G"));
        wire(c, k, QStringLiteral("C1"), QStringLiteral("COIL"));
        wire(c, k, QStringLiteral("C2"), QStringLiteral("G"));
        wire(c, k, QStringLiteral("COM"), QStringLiteral("COM"));
        wire(c, k, QStringLiteral("NO"), QStringLiteral("NO"));
        wire(c, k, QStringLiteral("NC"), QStringLiteral("NC"));
        resistor(c, QStringLiteral("1k"), QStringLiteral("NO"), QStringLiteral("G"));
        resistor(c, QStringLiteral("1k"), QStringLiteral("NC"), QStringLiteral("G"));
        c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
        const spice::Result res = simulate(c);
        QVERIFY2(res.ok, qPrintable(res.error));
        QVERIFY(qAbs(res.last(QStringLiteral("no")).value() - (energized ? 10 : 0)) < 0.1);
        QVERIFY(qAbs(res.last(QStringLiteral("nc")).value() - (energized ? 0 : 10)) < 0.1);
    }
}

void TestAnalog::transformerRatio()
{
    requireEngine();
    Circuit c;
    const ItemId t = part(c, QStringLiteral("transformer"), QStringLiteral("T1"), {},
                          {{QStringLiteral("lp"), QStringLiteral("100m")}, {QStringLiteral("ls"), QStringLiteral("1m")}});
    const ItemId g = part(c, QStringLiteral("siggen"), QStringLiteral("V1"), {},
                          {{QStringLiteral("amplitude"), QStringLiteral("10")}, {QStringLiteral("frequency"), QStringLiteral("1k")}});
    ground(c, QStringLiteral("G"));
    wire(c, g, QStringLiteral("+"), QStringLiteral("PRI"));
    wire(c, g, QStringLiteral("-"), QStringLiteral("G"));
    wire(c, t, QStringLiteral("P1"), QStringLiteral("PRI"));
    wire(c, t, QStringLiteral("P2"), QStringLiteral("G"));
    wire(c, t, QStringLiteral("S1"), QStringLiteral("SEC"));
    wire(c, t, QStringLiteral("S2"), QStringLiteral("G"));
    resistor(c, QStringLiteral("10k"), QStringLiteral("SEC"), QStringLiteral("G"));
    AnalysisSettings a{true, AnalysisSettings::Type::AC};
    a.acStart = QStringLiteral("1k");
    a.acStop = QStringLiteral("1k");
    a.acPointsPerDecade = 1;
    c.setAnalysis(a);
    const spice::Result res = simulate(c);
    QVERIFY2(res.ok, qPrintable(res.error));
    // sqrt(ls / lp) = 0.1, nearly unloaded
    const double gain = std::abs(res.find(QStringLiteral("sec"))->complex.value(0));
    QVERIFY2(qAbs(gain - 0.1) < 0.005, qPrintable(QString::number(gain)));
}

void TestAnalog::optoIsolator()
{
    requireEngine();
    for (const bool lit : {false, true}) {
        Circuit c;
        const ItemId u = part(c, QStringLiteral("optoisolator"), QStringLiteral("U1"));
        supply(c, QStringLiteral("IN"), lit ? QStringLiteral("5") : QStringLiteral("0"));
        supply(c, QStringLiteral("VCC"), QStringLiteral("5"));
        ground(c, QStringLiteral("G"));
        resistor(c, QStringLiteral("330"), QStringLiteral("IN"), QStringLiteral("A"));
        wire(c, u, QStringLiteral("A"), QStringLiteral("A"));
        wire(c, u, QStringLiteral("C"), QStringLiteral("G"));
        resistor(c, QStringLiteral("4.7k"), QStringLiteral("VCC"), QStringLiteral("OUT"));
        wire(c, u, QStringLiteral("COL"), QStringLiteral("OUT"));
        wire(c, u, QStringLiteral("E"), QStringLiteral("G"));
        c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
        spice::Deck deck;
        const spice::Result res = simulate(c, &deck);
        QVERIFY2(res.ok, qPrintable(res.error + u'\n' + deck.text));
        const double out = res.last(QStringLiteral("out")).value();
        QVERIFY2(lit ? out < 0.5 : out > 4.9, qPrintable(QString::number(out)));     // inverting pull-up stage
    }
}

void TestAnalog::scrLatches()
{
    requireEngine();
    Circuit c;
    const ItemId q = part(c, QStringLiteral("scr"), QStringLiteral("Q1"));
    const ItemId gate = part(c, QStringLiteral("siggen"), QStringLiteral("V1"), {},
                             {{QStringLiteral("wave"), QStringLiteral("pulse")}, {QStringLiteral("v1"), QStringLiteral("0")},
                              {QStringLiteral("v2"), QStringLiteral("5")}, {QStringLiteral("delay"), QStringLiteral("1m")},
                              {QStringLiteral("width"), QStringLiteral("0.1m")}, {QStringLiteral("period"), QStringLiteral("10")}});
    supply(c, QStringLiteral("VCC"), QStringLiteral("12"));
    ground(c, QStringLiteral("G"));
    wire(c, gate, QStringLiteral("+"), QStringLiteral("GP"));
    wire(c, gate, QStringLiteral("-"), QStringLiteral("G"));
    resistor(c, QStringLiteral("1k"), QStringLiteral("GP"), QStringLiteral("GATE"));
    wire(c, q, QStringLiteral("G"), QStringLiteral("GATE"));
    resistor(c, QStringLiteral("100"), QStringLiteral("VCC"), QStringLiteral("AN"));
    wire(c, q, QStringLiteral("A"), QStringLiteral("AN"));
    wire(c, q, QStringLiteral("K"), QStringLiteral("G"));
    transient(c, QStringLiteral("3m"), QStringLiteral("10u"));
    spice::Deck deck;
    const spice::Result res = simulate(c, &deck);
    QVERIFY2(res.ok, qPrintable(res.error + u'\n' + deck.text));
    const spice::Vector* t = res.find(QStringLiteral("time"));
    const spice::Vector* an = res.find(QStringLiteral("an"));
    auto at = [&](double when) {
        int i = 0;
        while (i + 1 < t->size() && t->real[i] < when) ++i;
        return an->real[i];
    };
    QVERIFY2(at(0.5e-3) > 11.5, qPrintable(QString::number(at(0.5e-3))));     // off before the pulse
    QVERIFY2(at(2.5e-3) < 2, qPrintable(QString::number(at(2.5e-3))));       // still on after it
}

void TestAnalog::triacBothWays()
{
    requireEngine();
    for (const double polarity : {1.0, -1.0}) {
        Circuit c;
        const ItemId q = part(c, QStringLiteral("triac"), QStringLiteral("Q1"));
        supply(c, QStringLiteral("SUP"), QString::number(12 * polarity));
        supply(c, QStringLiteral("GP"), QString::number(5 * polarity));
        ground(c, QStringLiteral("G"));
        resistor(c, QStringLiteral("100"), QStringLiteral("SUP"), QStringLiteral("MT2"));
        resistor(c, QStringLiteral("1k"), QStringLiteral("GP"), QStringLiteral("GATE"));
        wire(c, q, QStringLiteral("MT2"), QStringLiteral("MT2"));
        wire(c, q, QStringLiteral("G"), QStringLiteral("GATE"));
        wire(c, q, QStringLiteral("MT1"), QStringLiteral("G"));
        c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
        spice::Deck deck;
        const spice::Result res = simulate(c, &deck);
        QVERIFY2(res.ok, qPrintable(res.error + u'\n' + deck.text));
        const double v = res.last(QStringLiteral("mt2")).value();
        QVERIFY2(std::abs(v) < 2.5, qPrintable(QString::number(v)));            // conducting: most of 12 V on the 100 ohm
    }
}

void TestAnalog::deviceProbeCurrents()
{
    Circuit c;
    divider(c);
    for (const Component& comp : c.components()) {
        if (comp.designator != u"R1" && comp.designator != u"V1") continue;
        Component p = comp;
        p.props.insert(QStringLiteral("probe"), QStringLiteral("I"));
        c.updateComponent(p);
    }
    c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
    spice::Deck deck;
    const Netlist nl = NetlistBuilder::build(c, library());
    deck = spice::build(c, library(), nl);
    QVERIFY2(deck.text.contains(QStringLiteral(".save all @rr1[i]\n")), qPrintable(deck.text));   // sources need no .save
    ItemId r1 = 0, v1 = 0;
    for (const Component& comp : c.components()) {
        if (comp.designator == u"R1") r1 = comp.id;
        if (comp.designator == u"V1") v1 = comp.id;
    }
    QCOMPARE(deck.currentOf.value(r1), QStringLiteral("@rr1[i]"));
    QCOMPARE(deck.currentOf.value(v1), QStringLiteral("vv1#branch"));
    requireEngine();
    const spice::Result res = spice::NgspiceEngine::instance().run(deck.text);
    QVERIFY2(res.ok, qPrintable(res.error));
    QVERIFY2(qAbs(std::abs(res.last(QStringLiteral("@rr1[i]")).value_or(0)) - 5e-3) < 1e-9, qPrintable(res.log));
    QVERIFY(qAbs(std::abs(res.last(QStringLiteral("vv1#branch")).value_or(0)) - 5e-3) < 1e-9);
}

void TestAnalog::exportsSubcircuit()
{
    Circuit c;
    const ItemId a = part(c, QStringLiteral("resistor"), QStringLiteral("R1"), QStringLiteral("1k"));
    const ItemId b = part(c, QStringLiteral("resistor"), QStringLiteral("R2"), QStringLiteral("3k"));
    const ItemId in = part(c, QStringLiteral("port_in"), QStringLiteral("P1"), QStringLiteral("IN"));
    const ItemId out = part(c, QStringLiteral("port_out"), QStringLiteral("P2"), QStringLiteral("OUT"));
    label(c, a, QStringLiteral("1"), QStringLiteral("IN"));
    label(c, a, QStringLiteral("2"), QStringLiteral("OUT"));
    label(c, b, QStringLiteral("1"), QStringLiteral("OUT"));
    label(c, b, QStringLiteral("2"), QStringLiteral("G"));
    label(c, in, library().find(QStringLiteral("port_in"))->pins[0].name, QStringLiteral("IN"));
    label(c, out, library().find(QStringLiteral("port_out"))->pins[0].name, QStringLiteral("OUT"));
    ground(c, QStringLiteral("G"));
    QString error;
    const QString sub = spice::subcircuit(c, library(), NetlistBuilder::build(c, library()), QStringLiteral("Divider"), &error);
    QVERIFY2(sub.contains(QStringLiteral(".subckt divider in out\n")), qPrintable(sub + error));
    QVERIFY(sub.contains(QStringLiteral(".ends divider")));
    requireEngine();
    const spice::Result r = spice::NgspiceEngine::instance().run(
        sub + QStringLiteral("V1 a 0 DC 4\nX1 a b Divider\n.op\n.end\n"));
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + sub));
    QVERIFY(qAbs(r.last(QStringLiteral("b")).value_or(0) - 3) < 1e-6);

    Circuit none;
    part(none, QStringLiteral("resistor"), QStringLiteral("R1"));
    QVERIFY(spice::subcircuit(none, library(), NetlistBuilder::build(none, library()), QStringLiteral("x"), &error).isEmpty());
}

void TestAnalog::regulatorAndBridge()
{
    requireEngine();
    Circuit c;
    const ItemId br = part(c, QStringLiteral("fw_bridge"), QStringLiteral("D1"));
    const ItemId reg = part(c, QStringLiteral("regulator3"), QStringLiteral("U1"));
    supply(c, QStringLiteral("AC"), QStringLiteral("12"));
    ground(c, QStringLiteral("G"));
    wire(c, br, QStringLiteral("AC1"), QStringLiteral("AC"));
    wire(c, br, QStringLiteral("AC2"), QStringLiteral("G"));
    wire(c, br, QStringLiteral("+"), QStringLiteral("RAW"));
    wire(c, br, QStringLiteral("-"), QStringLiteral("NEG"));
    resistor(c, QStringLiteral("1k"), QStringLiteral("RAW"), QStringLiteral("NEG"));
    wire(c, reg, QStringLiteral("IN"), QStringLiteral("RAW"));
    wire(c, reg, QStringLiteral("GND"), QStringLiteral("NEG"));
    wire(c, reg, QStringLiteral("OUT"), QStringLiteral("OUT"));
    resistor(c, QStringLiteral("100"), QStringLiteral("OUT"), QStringLiteral("NEG"));
    c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    const double raw = r.last(QStringLiteral("raw")).value() - r.last(QStringLiteral("neg")).value();
    QVERIFY2(raw > 10 && raw < 11.5, qPrintable(QString::number(raw)));             // two diode drops
    const double out = r.last(QStringLiteral("out")).value() - r.last(QStringLiteral("neg")).value();
    QVERIFY2(qAbs(out - 5) < 0.01, qPrintable(QString::number(out)));
}

void TestAnalog::crystalResonance()
{
    requireEngine();
    Circuit c;
    const ItemId x = part(c, QStringLiteral("crystal"), QStringLiteral("Y1"), QStringLiteral("1Meg"));
    const ItemId g = part(c, QStringLiteral("siggen"), QStringLiteral("V1"));
    ground(c, QStringLiteral("G"));
    wire(c, g, QStringLiteral("+"), QStringLiteral("IN"));
    wire(c, g, QStringLiteral("-"), QStringLiteral("G"));
    wire(c, x, QStringLiteral("1"), QStringLiteral("IN"));
    wire(c, x, QStringLiteral("2"), QStringLiteral("OUT"));
    resistor(c, QStringLiteral("50"), QStringLiteral("OUT"), QStringLiteral("G"));
    AnalysisSettings a{true, AnalysisSettings::Type::AC};
    a.acStart = QStringLiteral("900k");
    a.acStop = QStringLiteral("1.1Meg");
    a.acPointsPerDecade = 2000;
    c.setAnalysis(a);
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    const spice::Vector* f = r.find(QStringLiteral("frequency"));
    const spice::Vector* out = r.find(QStringLiteral("out"));
    int peak = 0;
    for (int i = 0; i < out->size(); ++i)
        if (std::abs(out->complex[i]) > std::abs(out->complex[peak])) peak = i;
    const double fpeak = f->isComplex() ? f->complex[peak].real() : f->real[peak];
    QVERIFY2(qAbs(fpeak - 1e6) < 2e3, qPrintable(QString::number(fpeak)));          // series resonance
}

void TestAnalog::miscPartsSimulate_data()
{
    QTest::addColumn<QString>("symbol");
    for (const char* id : {"fuse", "njfet", "pnp_darlington", "buzzer", "speaker", "dc_motor", "solenoid", "triode", "tetrode"})
        QTest::newRow(id) << QString::fromLatin1(id);
}

// every pin through 10k to its own node, one pin driven by 10 V: the deck
// must build and reach an operating point
void TestAnalog::miscPartsSimulate()
{
    QFETCH(QString, symbol);
    requireEngine();
    Circuit c;
    const ItemId u = part(c, symbol, QStringLiteral("X1"));
    supply(c, QStringLiteral("VP"), QStringLiteral("10"));
    ground(c, QStringLiteral("G"));
    const SymbolDef& s = *library().find(symbol);
    for (int i = 0; i < s.pins.size(); ++i) {
        const QString net = QStringLiteral("N%1").arg(i);
        wire(c, u, s.pins[i].name, net);
        resistor(c, QStringLiteral("10k"), net, i == 0 ? QStringLiteral("VP") : QStringLiteral("G"));
    }
    c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
}

// A transistor labelled 2N2222A simulates with CircuitMaker's 2N2222A model
// (read from the installation); an unknown label keeps the default model.
void TestAnalog::labelledModels()
{
    if (cmmodels::find(QStringLiteral("2N2222A")).text.isEmpty())
        QSKIP("CircuitMaker models not installed");
    Circuit c;
    const ItemId q = place(c, QStringLiteral("npn"), {0, 0}, QStringLiteral("Q1"));
    Component t = *c.component(q);
    t.label = QStringLiteral("2N2222A");
    c.updateComponent(t);
    const ItemId q2 = place(c, QStringLiteral("npn"), {400, 0}, QStringLiteral("Q2"));
    Component u = *c.component(q2);
    u.label = QStringLiteral("NOSUCHPART");
    c.updateComponent(u);
    const spice::Deck deck = spice::build(c, library(), NetlistBuilder::build(c, library()));
    QVERIFY2(deck.text.contains(QStringLiteral(" CM_2N2222A\n")), qPrintable(deck.text));
    QVERIFY2(deck.text.contains(QStringLiteral(".MODEL CM_2N2222A NPN(")), qPrintable(deck.text));
    QVERIFY2(deck.text.contains(QStringLiteral(" Q2N3904\n")), qPrintable(deck.text));
}

void TestAnalog::importsSpiceNetlist()
{
    const QString netlist = QStringLiteral(
        "divider and a transistor\n"
        "* comment\n"
        "V1 in 0 DC 10\n"
        "R1 in out 1k ; inline comment\n"
        "R2 out 0\n+ 1k\n"
        "Q1 col out 0 QP\n"
        "Rc col 0 4.7k\n"                   // the transistor stays off
        "V2 sig 0 SIN(0 1 1k)\n"
        "Rs sig 0 1k\n"
        "X1 a b SOMESUB\n"
        ".model QP PNP(Bf=100)\n"
        ".op\n"
        ".end\n");
    Circuit c;
    SpiceImportReport rep;
    QString error;
    QVERIFY2(SpiceImporter::importText(netlist, c, library(), &rep, &error), qPrintable(error));
    QCOMPARE(rep.components, 7);
    QCOMPARE(rep.skipped.size(), 1);                                    // the X line
    QStringList symbols;
    for (const Component& comp : c.components())
        if (comp.symbolId != u"terminal" && comp.symbolId != u"ground") symbols << comp.designator + u'=' + comp.symbolId;
    symbols.sort();
    QCOMPARE(symbols, (QStringList{"Q1=pnp", "R1=resistor", "R2=resistor", "RC=resistor", "RS=resistor", "V1=vsource",
                                   "V2=siggen"}));
    QCOMPARE(c.analysis().type, AnalysisSettings::Type::OperatingPoint);
    requireEngine();
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    QVERIFY2(qAbs(r.last(QStringLiteral("out")).value_or(0) - 5) < 0.2, qPrintable(deck.text));   // the PNP barely loads it
}

void TestAnalog::controlledSources_data()
{
    QTest::addColumn<QString>("symbol");
    QTest::addColumn<QString>("gain");
    QTest::addColumn<double>("expected");          // |V(OUT)| with 1 V and 1 mA control, 1k load
    QTest::newRow("V->V") << "vcvs" << "2" << 2.0;
    QTest::newRow("V->I") << "vccs" << "3m" << 3.0;
    QTest::newRow("I->V") << "ccvs" << "4k" << 4.0;
    QTest::newRow("I->I") << "cccs" << "5" << 5.0;
    QTest::newRow("V->Switch on") << "vswitch" << "" << 10 * 1000 / 1001.0;
    QTest::newRow("I->Switch off") << "iswitch" << "" << 0.0;    // 1 mA is at the threshold, hysteresis keeps it open
}

void TestAnalog::controlledSources()
{
    QFETCH(QString, symbol);
    QFETCH(QString, gain);
    QFETCH(double, expected);
    requireEngine();
    Circuit c;
    const ItemId u = part(c, symbol, QStringLiteral("X1"), gain);
    ground(c, QStringLiteral("G"));
    // control: 1 V across 3-4 for voltage control; 1 mA through 3->4 for current control
    const bool currentControl = symbol == u"ccvs" || symbol == u"cccs" || symbol == u"iswitch";
    supply(c, QStringLiteral("CTL"), symbol.endsWith(u"switch") && !currentControl ? QStringLiteral("5") : QStringLiteral("1"));
    if (currentControl) {
        resistor(c, QStringLiteral("1k"), QStringLiteral("CTL"), QStringLiteral("C3"));
        wire(c, u, QStringLiteral("3"), QStringLiteral("C3"));
    } else {
        wire(c, u, QStringLiteral("3"), QStringLiteral("CTL"));
    }
    wire(c, u, QStringLiteral("4"), QStringLiteral("G"));
    if (symbol.endsWith(u"switch")) {
        supply(c, QStringLiteral("SUP"), QStringLiteral("10"));
        resistor(c, QStringLiteral("1k"), QStringLiteral("SUP"), QStringLiteral("OUT"));
        wire(c, u, QStringLiteral("1"), QStringLiteral("OUT"));
        wire(c, u, QStringLiteral("2"), QStringLiteral("G"));
        expected = expected > 0 ? 10 * 1 / 1001.0 : 10 * 1e9 / (1e9 + 1000);    // switch from OUT to ground
    } else {
        wire(c, u, QStringLiteral("1"), QStringLiteral("OUT"));
        wire(c, u, QStringLiteral("2"), QStringLiteral("G"));
        resistor(c, QStringLiteral("1k"), QStringLiteral("OUT"), QStringLiteral("G"));
    }
    c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    const double v = std::abs(r.last(QStringLiteral("out")).value_or(qQNaN()));
    QVERIFY2(qAbs(v - expected) < 0.01, qPrintable(QString::number(v) + u'\n' + deck.text));
}

void TestAnalog::converters()
{
    requireEngine();
    // ADC of 2.5 V on a 0-5 V range feeds the DAC: 128 = only D7, back to 2.5 V
    Circuit c;
    const ItemId adc = part(c, QStringLiteral("adc8"), QStringLiteral("U1"));
    const ItemId dac = part(c, QStringLiteral("dac8"), QStringLiteral("U2"));
    supply(c, QStringLiteral("VIN"), QStringLiteral("2.5"));
    supply(c, QStringLiteral("REF"), QStringLiteral("5"));
    ground(c, QStringLiteral("G"));
    wire(c, adc, QStringLiteral("Vin"), QStringLiteral("VIN"));
    for (const ItemId u : {adc, dac}) {
        wire(c, u, QStringLiteral("VREF+"), QStringLiteral("REF"));
        wire(c, u, QStringLiteral("VREF-"), QStringLiteral("G"));
    }
    for (int i = 0; i < 8; ++i) {
        wire(c, adc, QStringLiteral("D%1").arg(i), QStringLiteral("B%1").arg(i));
        wire(c, dac, QStringLiteral("D%1").arg(i), QStringLiteral("B%1").arg(i));
    }
    wire(c, dac, QStringLiteral("Vout"), QStringLiteral("OUT"));
    c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    QVERIFY(qAbs(r.last(QStringLiteral("b7")).value_or(0) - 5) < 1e-6);
    QVERIFY(qAbs(r.last(QStringLiteral("b6")).value_or(1)) < 1e-6);
    QVERIFY2(qAbs(r.last(QStringLiteral("out")).value_or(0) - 2.5) < 1e-6, qPrintable(deck.text));
}

void TestAnalog::blocksAndExpressions()
{
    requireEngine();
    Circuit c;
    supply(c, QStringLiteral("A"), QStringLiteral("6"));
    supply(c, QStringLiteral("B"), QStringLiteral("2"));
    ground(c, QStringLiteral("G"));
    const ItemId gain = part(c, QStringLiteral("gain_block"), QStringLiteral("A1"), {}, {{QStringLiteral("gain"), QStringLiteral("3")}});
    wire(c, gain, QStringLiteral("IN"), QStringLiteral("B"));
    wire(c, gain, QStringLiteral("OUT"), QStringLiteral("GAIN"));
    const ItemId div = part(c, QStringLiteral("divider_block"), QStringLiteral("A2"));
    wire(c, div, QStringLiteral("NUM"), QStringLiteral("A"));
    wire(c, div, QStringLiteral("DEN"), QStringLiteral("B"));
    wire(c, div, QStringLiteral("OUT"), QStringLiteral("QUOT"));
    const ItemId nl = part(c, QStringLiteral("nlv"), QStringLiteral("B1"), {}, {{QStringLiteral("expr"), QStringLiteral("v(a)*v(b)")}});
    wire(c, nl, QStringLiteral("V+"), QStringLiteral("PROD"));
    wire(c, nl, QStringLiteral("V-"), QStringLiteral("G"));
    for (const char* n : {"GAIN", "QUOT", "PROD"}) resistor(c, QStringLiteral("1k"), QString::fromLatin1(n), QStringLiteral("G"));
    c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    QVERIFY(qAbs(r.last(QStringLiteral("gain")).value_or(0) - 6) < 1e-6);
    QVERIFY(qAbs(r.last(QStringLiteral("quot")).value_or(0) - 3) < 1e-6);
    QVERIFY(qAbs(r.last(QStringLiteral("prod")).value_or(0) - 12) < 1e-6);
}

// V-Math1/2 (the label picks the function), their floating-output versions
// and the differential gain and divider blocks
void TestAnalog::mathBlocks()
{
    requireEngine();
    Circuit c;
    supply(c, QStringLiteral("A"), QStringLiteral("4"));
    supply(c, QStringLiteral("B"), QStringLiteral("2"));
    ground(c, QStringLiteral("G"));
    const QString S = QStringLiteral("SQRT"), M = QStringLiteral("MULT"), U = QStringLiteral("NEG"), K = QStringLiteral("GAIN"),
                  D = QStringLiteral("DIV");
    const ItemId sq = part(c, QStringLiteral("vmath1"), QStringLiteral("A1"), QStringLiteral("SQRTV"));
    wire(c, sq, QStringLiteral("V"), QStringLiteral("A"));
    wire(c, sq, QStringLiteral("Q"), S);
    const ItemId mul = part(c, QStringLiteral("vmath2"), QStringLiteral("A2"), QStringLiteral("MULTV"));
    wire(c, mul, QStringLiteral("A"), QStringLiteral("A"));
    wire(c, mul, QStringLiteral("B"), QStringLiteral("B"));
    wire(c, mul, QStringLiteral("Q"), M);
    const ItemId neg = part(c, QStringLiteral("vmath1_ref"), QStringLiteral("A3"), QStringLiteral("UNARYVR"));
    wire(c, neg, QStringLiteral("V+"), QStringLiteral("A"));
    wire(c, neg, QStringLiteral("V-"), QStringLiteral("B"));
    wire(c, neg, QStringLiteral("Q+"), U);
    wire(c, neg, QStringLiteral("Q-"), QStringLiteral("G"));
    const ItemId gain = part(c, QStringLiteral("gain_diff"), QStringLiteral("A4"), {}, {{QStringLiteral("gain"), QStringLiteral("3")}});
    wire(c, gain, QStringLiteral("IN+"), QStringLiteral("A"));
    wire(c, gain, QStringLiteral("IN-"), QStringLiteral("B"));
    wire(c, gain, QStringLiteral("OUT+"), K);
    wire(c, gain, QStringLiteral("OUT-"), QStringLiteral("G"));
    const ItemId div = part(c, QStringLiteral("divider_diff"), QStringLiteral("A5"));
    wire(c, div, QStringLiteral("NUM+"), QStringLiteral("A"));
    wire(c, div, QStringLiteral("NUM-"), QStringLiteral("G"));
    wire(c, div, QStringLiteral("DEN+"), QStringLiteral("B"));
    wire(c, div, QStringLiteral("DEN-"), QStringLiteral("G"));
    wire(c, div, QStringLiteral("OUT+"), D);
    wire(c, div, QStringLiteral("OUT-"), QStringLiteral("G"));
    for (const QString& n : {S, M, U, K, D}) resistor(c, QStringLiteral("1k"), n, QStringLiteral("G"));
    c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    const QPair<QString, double> expected[] = {{QStringLiteral("sqrt"), 2}, {QStringLiteral("mult"), 8}, {QStringLiteral("neg"), -2},
                                               {QStringLiteral("gain"), 6}, {QStringLiteral("div"), 2}};
    for (const auto& [node, v] : expected)
        QVERIFY2(qAbs(r.last(node).value_or(1e9) - v) < 1e-6, qPrintable(node + u' ' + QString::number(r.last(node).value_or(1e9))));
}

// Transformers reflect their load (a 1:10 ratio through 1k: 9.09 V on the
// primary), the centre tap splits the secondary, a core transformer follows
// its turns; relay contacts follow the coil of the same name; a thrown SPDT
// switch connects C to NO.
void TestAnalog::cmDevices()
{
    requireEngine();
    Circuit c;
    supply(c, QStringLiteral("A"), QStringLiteral("10"));
    supply(c, QStringLiteral("B"), QStringLiteral("2"));
    supply(c, QStringLiteral("ONE"), QStringLiteral("1"));
    ground(c, QStringLiteral("G"));
    resistor(c, QStringLiteral("1k"), QStringLiteral("A"), QStringLiteral("P"));
    const ItemId t = part(c, QStringLiteral("trans_ratio"), QStringLiteral("T1"), {}, {{QStringLiteral("ratio"), QStringLiteral("0.1")}});
    wire(c, t, QStringLiteral("P1"), QStringLiteral("P"));
    wire(c, t, QStringLiteral("P2"), QStringLiteral("G"));
    wire(c, t, QStringLiteral("S1"), QStringLiteral("SEC"));
    wire(c, t, QStringLiteral("S2"), QStringLiteral("G"));
    resistor(c, QStringLiteral("100"), QStringLiteral("SEC"), QStringLiteral("G"));
    const ItemId ct = part(c, QStringLiteral("trans_ct"), QStringLiteral("T2"), {}, {{QStringLiteral("ratio"), QStringLiteral("0.2")}});
    wire(c, ct, QStringLiteral("P1"), QStringLiteral("A"));
    wire(c, ct, QStringLiteral("P2"), QStringLiteral("G"));
    wire(c, ct, QStringLiteral("S1"), QStringLiteral("TOP"));
    wire(c, ct, QStringLiteral("CT"), QStringLiteral("G"));
    wire(c, ct, QStringLiteral("S2"), QStringLiteral("BOT"));
    const ItemId x = part(c, QStringLiteral("xfmr"), QStringLiteral("T3"), {},
                          {{QStringLiteral("prit"), QStringLiteral("10")}, {QStringLiteral("sect"), QStringLiteral("100")}});
    wire(c, x, QStringLiteral("P1"), QStringLiteral("ONE"));
    wire(c, x, QStringLiteral("P2"), QStringLiteral("G"));
    wire(c, x, QStringLiteral("S1"), QStringLiteral("UP"));
    wire(c, x, QStringLiteral("S2"), QStringLiteral("G"));
    for (const QString& n : {QStringLiteral("TOP"), QStringLiteral("BOT"), QStringLiteral("UP")})
        resistor(c, QStringLiteral("1k"), n, QStringLiteral("G"));

    // K1 pulled in by 10 V, K2 not by 1 V
    const QPair<QString, QString> coils[] = {{QStringLiteral("K1"), QStringLiteral("A")}, {QStringLiteral("K2"), QStringLiteral("ONE")}};
    for (const auto& [relay, from] : coils) {
        const ItemId k = part(c, QStringLiteral("relay_coil"), QStringLiteral("K") + relay, {}, {{QStringLiteral("relay"), relay}});
        wire(c, k, QStringLiteral("1"), from);
        wire(c, k, QStringLiteral("2"), QStringLiteral("G"));
    }
    const struct { const char* id; const char* relay; const char* out; } contacts[] = {
        {"contact_no", "K1", "NO1"}, {"contact_nc", "K1", "NC1"}, {"contact_no", "K2", "NO2"}};
    int n = 0;
    for (const auto& k : contacts) {
        const QString id = QString::fromLatin1(k.id), out = QString::fromLatin1(k.out);
        const ItemId ck = part(c, id, QStringLiteral("KC%1").arg(++n), {}, {{QStringLiteral("relay"), QString::fromLatin1(k.relay)}});
        wire(c, ck, QStringLiteral("C"), QStringLiteral("B"));
        wire(c, ck, id == u"contact_no" ? QStringLiteral("NO") : QStringLiteral("NC"), out);
        resistor(c, QStringLiteral("1k"), out, QStringLiteral("G"));
    }
    const ItemId sw = part(c, QStringLiteral("spdt"), QStringLiteral("S1"), {}, {{QStringLiteral("closed"), 1}});
    wire(c, sw, QStringLiteral("C"), QStringLiteral("B"));
    wire(c, sw, QStringLiteral("NC"), QStringLiteral("SWNC"));
    wire(c, sw, QStringLiteral("NO"), QStringLiteral("SWNO"));
    resistor(c, QStringLiteral("1k"), QStringLiteral("SWNC"), QStringLiteral("G"));
    resistor(c, QStringLiteral("1k"), QStringLiteral("SWNO"), QStringLiteral("G"));

    c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    const QPair<QString, double> expected[] = {
        {QStringLiteral("p"), 9.09}, {QStringLiteral("sec"), 0.909}, {QStringLiteral("top"), 1}, {QStringLiteral("bot"), -1},
        {QStringLiteral("up"), 10}, {QStringLiteral("no1"), 2}, {QStringLiteral("nc1"), 0}, {QStringLiteral("no2"), 0},
        {QStringLiteral("swno"), 2}, {QStringLiteral("swnc"), 0}};
    for (const auto& [node, v] : expected)
        QVERIFY2(qAbs(r.last(node).value_or(1e9) - v) < 0.02 * std::max(1.0, qAbs(v)),
                 qPrintable(node + u' ' + QString::number(r.last(node).value_or(1e9)) + u'\n' + deck.text));
}

// A square-wave VCO at 2 V runs at 2 kHz; a PLL locks its VCO onto a 12 kHz input.
void TestAnalog::vcoAndPll()
{
    requireEngine();
    {
        Circuit c;
        supply(c, QStringLiteral("CTL"), QStringLiteral("2"));
        ground(c, QStringLiteral("G"));
        const ItemId v = part(c, QStringLiteral("vco"), QStringLiteral("A1"), QStringLiteral("SQRVCO"),
                              {{QStringLiteral("low"), QStringLiteral("0")}, {QStringLiteral("high"), QStringLiteral("5")}});
        wire(c, v, QStringLiteral("C+"), QStringLiteral("CTL"));
        wire(c, v, QStringLiteral("C-"), QStringLiteral("G"));
        wire(c, v, QStringLiteral("OUT+"), QStringLiteral("OUT"));
        wire(c, v, QStringLiteral("OUT-"), QStringLiteral("G"));
        resistor(c, QStringLiteral("1k"), QStringLiteral("OUT"), QStringLiteral("G"));
        transient(c, QStringLiteral("5m"), QStringLiteral("5u"));
        spice::Deck deck;
        const spice::Result r = simulate(c, &deck);
        QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
        const QVector<double> edges = risingEdges(r, QStringLiteral("out"), 2.5);
        QVERIFY(edges.size() >= 5);
        QVERIFY2(qAbs((edges.last() - edges[1]) / (edges.size() - 2) - 0.5e-3) < 0.02e-3, qPrintable(deck.text));
    }
    Circuit c;
    supply(c, QStringLiteral("VDD"), QStringLiteral("5"));
    ground(c, QStringLiteral("G"));
    const ItemId g = part(c, QStringLiteral("siggen"), QStringLiteral("V1"), {},
                          {{QStringLiteral("wave"), QStringLiteral("sine")}, {QStringLiteral("offset"), QStringLiteral("2.5")},
                           {QStringLiteral("amplitude"), QStringLiteral("2.5")}, {QStringLiteral("frequency"), QStringLiteral("12k")}});
    wire(c, g, QStringLiteral("+"), QStringLiteral("SIG"));
    wire(c, g, QStringLiteral("-"), QStringLiteral("G"));
    const ItemId pll = part(c, QStringLiteral("pll"), QStringLiteral("U1"));
    const QPair<QString, QString> pins[] = {{QStringLiteral("Sig"), QStringLiteral("SIG")}, {QStringLiteral("Comp"), QStringLiteral("OUT")},
                                            {QStringLiteral("Vout"), QStringLiteral("OUT")}, {QStringLiteral("Vss"), QStringLiteral("G")},
                                            {QStringLiteral("Dem"), QStringLiteral("DEM")}, {QStringLiteral("Vin"), QStringLiteral("VIN")},
                                            {QStringLiteral("PC"), QStringLiteral("PC")}, {QStringLiteral("Vdd"), QStringLiteral("VDD")}};
    for (const auto& [pin, net] : pins) wire(c, pll, pin, net);
    resistor(c, QStringLiteral("10k"), QStringLiteral("PC"), QStringLiteral("VIN"));
    const ItemId cap = part(c, QStringLiteral("capacitor"), QStringLiteral("C1"), QStringLiteral("10n"));
    wire(c, cap, QStringLiteral("1"), QStringLiteral("VIN"));
    wire(c, cap, QStringLiteral("2"), QStringLiteral("G"));
    transient(c, QStringLiteral("20m"), QStringLiteral("1u"));
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    QVector<double> edges;
    for (double t : risingEdges(r, QStringLiteral("out"), 2.5))
        if (t > 15e-3) edges << t;
    QVERIFY(edges.size() > 20);
    const double f = (edges.size() - 1) / (edges.last() - edges.first());
    QVERIFY2(qAbs(f - 12e3) < 100, qPrintable(QString::number(f)));
}

void TestAnalog::transmissionLineDelay()
{
    requireEngine();
    Circuit c;
    const ItemId t = part(c, QStringLiteral("tline"), QStringLiteral("T1"), {}, {{QStringLiteral("td"), QStringLiteral("1u")}});
    const ItemId g = part(c, QStringLiteral("siggen"), QStringLiteral("V1"), {},
                          {{QStringLiteral("wave"), QStringLiteral("pulse")}, {QStringLiteral("v1"), QStringLiteral("0")},
                           {QStringLiteral("v2"), QStringLiteral("2")}, {QStringLiteral("width"), QStringLiteral("10u")},
                           {QStringLiteral("period"), QStringLiteral("20u")}});
    ground(c, QStringLiteral("G"));
    wire(c, g, QStringLiteral("+"), QStringLiteral("SRC"));
    wire(c, g, QStringLiteral("-"), QStringLiteral("G"));
    resistor(c, QStringLiteral("50"), QStringLiteral("SRC"), QStringLiteral("IN"));      // matched
    wire(c, t, QStringLiteral("N1+"), QStringLiteral("IN"));
    wire(c, t, QStringLiteral("N1-"), QStringLiteral("G"));
    wire(c, t, QStringLiteral("N2+"), QStringLiteral("OUT"));
    wire(c, t, QStringLiteral("N2-"), QStringLiteral("G"));
    resistor(c, QStringLiteral("50"), QStringLiteral("OUT"), QStringLiteral("G"));
    transient(c, QStringLiteral("3u"), QStringLiteral("10n"));
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    const QVector<double> edges = risingEdges(r, QStringLiteral("out"), 0.5);
    QVERIFY2(!edges.isEmpty() && qAbs(edges.first() - 1e-6) < 0.05e-6, qPrintable(QString::number(edges.value(0))));
}

void TestAnalog::unijunctions()
{
    requireEngine();
    // UJT relaxation oscillator: C charges through 47k until the emitter fires
    Circuit c;
    const ItemId q = part(c, QStringLiteral("ujt"), QStringLiteral("Q1"));
    supply(c, QStringLiteral("VCC"), QStringLiteral("12"));
    ground(c, QStringLiteral("G"));
    resistor(c, QStringLiteral("47k"), QStringLiteral("VCC"), QStringLiteral("E"));
    const ItemId cap = part(c, QStringLiteral("capacitor"), QStringLiteral("C1"), QStringLiteral("100n"));
    wire(c, cap, QStringLiteral("1"), QStringLiteral("E"));
    wire(c, cap, QStringLiteral("2"), QStringLiteral("G"));
    wire(c, q, QStringLiteral("E"), QStringLiteral("E"));
    resistor(c, QStringLiteral("470"), QStringLiteral("VCC"), QStringLiteral("B2"));
    wire(c, q, QStringLiteral("B2"), QStringLiteral("B2"));
    resistor(c, QStringLiteral("47"), QStringLiteral("B1"), QStringLiteral("G"));
    wire(c, q, QStringLiteral("B1"), QStringLiteral("B1"));
    transient(c, QStringLiteral("30m"), QStringLiteral("10u"));
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    QVERIFY2(risingEdges(r, QStringLiteral("b1"), 0.5).size() >= 2, "no oscillation");

    // PUT: off while the anode is below the gate
    Circuit p;
    const ItemId put = part(p, QStringLiteral("put"), QStringLiteral("Q1"));
    supply(p, QStringLiteral("VA"), QStringLiteral("5"));
    supply(p, QStringLiteral("VG"), QStringLiteral("8"));
    ground(p, QStringLiteral("G"));
    resistor(p, QStringLiteral("1k"), QStringLiteral("VA"), QStringLiteral("AN"));
    resistor(p, QStringLiteral("10k"), QStringLiteral("VG"), QStringLiteral("GATE"));
    wire(p, put, QStringLiteral("A"), QStringLiteral("AN"));
    wire(p, put, QStringLiteral("G"), QStringLiteral("GATE"));
    wire(p, put, QStringLiteral("K"), QStringLiteral("G"));
    p.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
    const spice::Result rp = simulate(p, &deck);
    QVERIFY2(rp.ok, qPrintable(rp.error + u'\n' + deck.text));
    QVERIFY2(rp.last(QStringLiteral("an")).value_or(0) > 4.9, qPrintable(QString::number(rp.last(QStringLiteral("an")).value_or(0))));
}

void TestAnalog::ownSpiceLine()
{
    requireEngine();
    Circuit c;
    divider(c);
    // R2 becomes a subcircuit of two 500 ohm resistors, and a diode with its own model
    for (const Component& comp : c.components()) {
        if (comp.designator != u"R2") continue;
        Component r = comp;
        r.props.insert(QStringLiteral("spice"), QStringLiteral("X{name} {1} {2} HALVES"));
        r.props.insert(QStringLiteral("spiceModels"), QStringLiteral(".subckt HALVES a b\nR1 a m 500\nR2 m b 500\n.ends HALVES"));
        c.updateComponent(r);
    }
    c.setAnalysis({true, AnalysisSettings::Type::OperatingPoint});
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    QVERIFY2(deck.text.contains(QStringLiteral("Xr2 out 0 HALVES")), qPrintable(deck.text));
    QVERIFY(qAbs(r.last(QStringLiteral("out")).value_or(0) - 5) < 1e-6);
}

// An MSI chip inside the analog simulation runs the digital model through
// XSPICE d_cosim: a 7493 counts the pulser's falling edges.
void TestAnalog::mixedModeCounter()
{
    requireEngine();
    if (spice::cosimLibrary().isEmpty())
        QSKIP("co-simulation library not built");
    Circuit c;
    const ItemId p = part(c, QStringLiteral("pulser"), QStringLiteral("P1"), {},
                          {{QStringLiteral("high"), 10}, {QStringLiteral("low"), 10}});      // 20 us period
    const ItemId u = part(c, QStringLiteral("7493"), QStringLiteral("U1"));
    wire(c, p, QStringLiteral("Q"), QStringLiteral("CLK"));
    wire(c, u, QStringLiteral("~CKA"), QStringLiteral("CLK"));
    wire(c, u, QStringLiteral("QA"), QStringLiteral("QA"));
    wire(c, u, QStringLiteral("~CKB"), QStringLiteral("QA"));
    for (const char* q : {"QB", "QC", "QD"}) wire(c, u, QString::fromLatin1(q), QString::fromLatin1(q));
    wire(c, u, QStringLiteral("R01"), QStringLiteral("G"));
    wire(c, u, QStringLiteral("R02"), QStringLiteral("G"));
    ground(c, QStringLiteral("G"));
    for (const char* q : {"QA", "QB", "QC", "QD"}) resistor(c, QStringLiteral("10k"), QString::fromLatin1(q), QStringLiteral("G"));
    transient(c, QStringLiteral("200u"), QStringLiteral("0.5u"));
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    QVERIFY2(deck.text.contains(QStringLiteral("d_cosim")), qPrintable(deck.text));
    const spice::Vector* t = r.find(QStringLiteral("time"));
    auto count = [&](double when) {
        int i = 0;
        while (i + 1 < t->size() && t->real[i] < when) ++i;
        int v = 0, bit = 0;
        for (const char* q : {"qa", "qb", "qc", "qd"})
            v |= int(r.find(QString::fromLatin1(q))->real[i] > 2.5) << bit++;
        return v;
    };
    // the pulser starts low, rises at 10 us and falls at 20 us, 40 us, ...
    QCOMPARE(count(15e-6), 0);
    QCOMPARE(count(25e-6), 1);
    QCOMPARE(count(105e-6), 5);
    QCOMPARE(count(185e-6), 9);
}

// A RAM in the analog simulation: its data pins float while it writes and
// drive while it reads. D0 is fed through 1k from a source that is 5 V
// while the RAM writes and 0 V after; reading, the RAM holds D0 high.
void TestAnalog::mixedModeRam()
{
    requireEngine();
    if (spice::cosimLibrary().isEmpty())
        QSKIP("co-simulation library not built");
    Circuit c;
    const ItemId u = part(c, QStringLiteral("RAM1K"), QStringLiteral("U1"));
    for (int i = 0; i < 10; ++i) wire(c, u, QStringLiteral("A%1").arg(i), QStringLiteral("G"));
    wire(c, u, QStringLiteral("~CS"), QStringLiteral("G"));
    wire(c, u, QStringLiteral("~WE"), QStringLiteral("WE"));
    wire(c, u, QStringLiteral("D0"), QStringLiteral("D0"));
    wire(c, u, QStringLiteral("D1"), QStringLiteral("D1"));
    ground(c, QStringLiteral("G"));
    auto pwl = [&](const QString& net, const QString& points) {
        const ItemId g = part(c, QStringLiteral("siggen"), QStringLiteral("V%1").arg(c.components().size()), {},
                              {{QStringLiteral("wave"), QStringLiteral("pwl")}, {QStringLiteral("pwl"), points}});
        wire(c, g, QStringLiteral("+"), net);
        wire(c, g, QStringLiteral("-"), QStringLiteral("G"));
    };
    pwl(QStringLiteral("WE"), QStringLiteral("0 0\n50u 0\n51u 5\n200u 5"));          // write, then read
    pwl(QStringLiteral("SRC"), QStringLiteral("0 5\n70u 5\n71u 0\n200u 0"));
    resistor(c, QStringLiteral("1k"), QStringLiteral("SRC"), QStringLiteral("D0"));
    resistor(c, QStringLiteral("1k"), QStringLiteral("G"), QStringLiteral("D1"));
    transient(c, QStringLiteral("150u"), QStringLiteral("0.5u"));
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    QVERIFY2(deck.text.contains(QStringLiteral("cm_tsw")), qPrintable(deck.text));
    const spice::Vector* t = r.find(QStringLiteral("time"));
    auto at = [&](const char* net, double when) {
        int i = 0;
        while (i + 1 < t->size() && t->real[i] < when) ++i;
        return r.find(QString::fromLatin1(net))->real[i];
    };
    QVERIFY2(at("d0", 30e-6) > 4.9, qPrintable(QString::number(at("d0", 30e-6))));     // writing: the source alone
    QVERIFY2(at("d1", 30e-6) < 0.1, qPrintable(QString::number(at("d1", 30e-6))));
    QVERIFY2(at("d0", 120e-6) > 4.5, qPrintable(QString::number(at("d0", 120e-6))));   // reading: the stored 1
    QVERIFY2(at("d1", 120e-6) < 0.5, qPrintable(QString::number(at("d1", 120e-6))));
}

// A disabled tri-state buffer floats in the analog simulation: Y follows
// the 2 V it is pulled to; enabled, it drives its input's 5 V.
void TestAnalog::mixedModeTristate()
{
    requireEngine();
    Circuit c;
    const ItemId b = part(c, QStringLiteral("tribuf"), QStringLiteral("U1"));
    wire(c, b, QStringLiteral("A"), QStringLiteral("HI"));
    wire(c, b, QStringLiteral("EN"), QStringLiteral("EN"));
    wire(c, b, QStringLiteral("Y"), QStringLiteral("Y"));
    supply(c, QStringLiteral("HI"), QStringLiteral("5"));
    supply(c, QStringLiteral("TWO"), QStringLiteral("2"));
    resistor(c, QStringLiteral("1k"), QStringLiteral("TWO"), QStringLiteral("Y"));
    const ItemId g = part(c, QStringLiteral("siggen"), QStringLiteral("V9"), {},
                          {{QStringLiteral("wave"), QStringLiteral("pwl")}, {QStringLiteral("pwl"), QStringLiteral("0 0\n50u 0\n51u 5\n100u 5")}});
    wire(c, g, QStringLiteral("+"), QStringLiteral("EN"));
    wire(c, g, QStringLiteral("-"), QStringLiteral("G"));
    ground(c, QStringLiteral("G"));
    transient(c, QStringLiteral("100u"), QStringLiteral("0.5u"));
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    const spice::Vector* t = r.find(QStringLiteral("time"));
    auto at = [&](double when) {
        int i = 0;
        while (i + 1 < t->size() && t->real[i] < when) ++i;
        return r.find(QStringLiteral("y"))->real[i];
    };
    QVERIFY2(qAbs(at(30e-6) - 2) < 0.05, qPrintable(QString::number(at(30e-6))));
    QVERIFY2(at(90e-6) > 4.9, qPrintable(QString::number(at(90e-6))));
    QVERIFY(deck.warnings.filter(QStringLiteral("2.5 V")).isEmpty());
}

// A NAND gate with RC feedback has no operating point: the transient
// starts from the initial conditions instead, and oscillates (Osc1.ckt).
void TestAnalog::gateOscillatorFromInitialConditions()
{
    requireEngine();
    const QString path = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits/Osc1.ckt");
    if (!QFile::exists(path))
        QSKIP("Osc1.ckt not available");
    Circuit c;
    QVERIFY(CktImporter::import(path, c, library(), nullptr, nullptr));
    transient(c, QStringLiteral("2m"), QStringLiteral("2u"));
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(r.fromInitialConditions);
    int bestCrossings = 0;
    for (auto it = r.vectors.cbegin(); it != r.vectors.cend(); ++it) {
        if (it.key() == u"time" || it.value().isComplex()) continue;
        int crossings = 0;
        for (int i = 1; i < it.value().size(); ++i)
            crossings += (it.value().real[i - 1] < 2.5) != (it.value().real[i] < 2.5);
        bestCrossings = std::max(bestCrossings, crossings);
    }
    QVERIFY2(bestCrossings >= 4, qPrintable(QString::number(bestCrossings)));
}

// F->V Source: a 2 kHz square wave at 1k cycles per volt gives 2 V.
void TestAnalog::frequencyToVoltage()
{
    requireEngine();
    if (spice::cosimLibrary().isEmpty())
        QSKIP("co-simulation library not built");
    Circuit c;
    const ItemId g = part(c, QStringLiteral("siggen"), QStringLiteral("V1"), {},
                          {{QStringLiteral("wave"), QStringLiteral("pulse")}, {QStringLiteral("low"), QStringLiteral("0")},
                           {QStringLiteral("high"), QStringLiteral("5")}, {QStringLiteral("period"), QStringLiteral("500u")},
                           {QStringLiteral("width"), QStringLiteral("250u")}});
    wire(c, g, QStringLiteral("+"), QStringLiteral("IN"));
    wire(c, g, QStringLiteral("-"), QStringLiteral("G"));
    const ItemId f = part(c, QStringLiteral("ftov"), QStringLiteral("V2"));
    wire(c, f, QStringLiteral("NC+"), QStringLiteral("IN"));
    wire(c, f, QStringLiteral("NC-"), QStringLiteral("G"));
    wire(c, f, QStringLiteral("N+"), QStringLiteral("OUT"));
    wire(c, f, QStringLiteral("N-"), QStringLiteral("G"));
    resistor(c, QStringLiteral("10k"), QStringLiteral("OUT"), QStringLiteral("G"));
    ground(c, QStringLiteral("G"));
    transient(c, QStringLiteral("3m"), QStringLiteral("5u"));
    spice::Deck deck;
    const spice::Result r = simulate(c, &deck);
    QVERIFY2(r.ok, qPrintable(r.error + u'\n' + deck.text));
    const double v = r.last(QStringLiteral("out")).value_or(0);
    QVERIFY2(qAbs(v - 2) < 0.02, qPrintable(QString::number(v) + u'\n' + deck.text));
}

// MC1496: carrier and signal multiply; with no signal the balanced output
// carries no carrier.
void TestAnalog::balancedModulator()
{
    requireEngine();
    double swings[2] = {};
    for (int k = 0; k < 2; ++k) {
        Circuit c;
        const ItemId u = part(c, QStringLiteral("mc1496"), QStringLiteral("U1"));
        supply(c, QStringLiteral("VCC"), QStringLiteral("12"));
        supply(c, QStringLiteral("VEE"), QStringLiteral("-8"));
        wire(c, u, QStringLiteral("VEE"), QStringLiteral("VEE"));
        resistor(c, QStringLiteral("6.8k"), QStringLiteral("G"), QStringLiteral("BIAS"));   // about 1 mA (datasheet)
        wire(c, u, QStringLiteral("BIAS"), QStringLiteral("BIAS"));
        resistor(c, QStringLiteral("1k"), QStringLiteral("G1"), QStringLiteral("G2"));
        wire(c, u, QStringLiteral("G1"), QStringLiteral("G1"));
        wire(c, u, QStringLiteral("G2"), QStringLiteral("G2"));
        wire(c, u, QStringLiteral("OUT+"), QStringLiteral("OUTA"));
        wire(c, u, QStringLiteral("OUT-"), QStringLiteral("OUTB"));
        resistor(c, QStringLiteral("3.9k"), QStringLiteral("VCC"), QStringLiteral("OUTA"));
        resistor(c, QStringLiteral("3.9k"), QStringLiteral("VCC"), QStringLiteral("OUTB"));
        // carrier: 0.1 V of 10 kHz on VC+ around VC- at 6 V; the signal a DC level
        supply(c, QStringLiteral("CREF"), QStringLiteral("6"));
        const ItemId g = part(c, QStringLiteral("siggen"), QStringLiteral("V9"), {},
                              {{QStringLiteral("frequency"), QStringLiteral("10k")}, {QStringLiteral("amplitude"), QStringLiteral("0.1")}});
        wire(c, g, QStringLiteral("+"), QStringLiteral("VCA"));
        wire(c, g, QStringLiteral("-"), QStringLiteral("CREF"));
        wire(c, u, QStringLiteral("VC+"), QStringLiteral("VCA"));
        wire(c, u, QStringLiteral("VC-"), QStringLiteral("CREF"));
        supply(c, QStringLiteral("SIG"), k ? QStringLiteral("0.1") : QStringLiteral("0"));
        wire(c, u, QStringLiteral("VS+"), QStringLiteral("SIG"));
        wire(c, u, QStringLiteral("VS-"), QStringLiteral("G"));
        ground(c, QStringLiteral("G"));
        transient(c, QStringLiteral("300u"), QStringLiteral("1u"));
        const spice::Result r = simulate(c);
        QVERIFY2(r.ok, qPrintable(r.error));
        const spice::Vector* a = r.find(QStringLiteral("outa"));
        const spice::Vector* b = r.find(QStringLiteral("outb"));
        QVERIFY(a && b);
        double lo = 1e9, hi = -1e9;
        for (int i = a->size() / 2; i < a->size(); ++i) {
            lo = std::min(lo, a->real[i] - b->real[i]);
            hi = std::max(hi, a->real[i] - b->real[i]);
        }
        swings[k] = hi - lo;
    }
    // balanced, the carrier is suppressed; a signal lets it through
    QVERIFY2(swings[0] < 0.05, qPrintable(QString::number(swings[0])));
    QVERIFY2(swings[1] > 10 * swings[0] && swings[1] > 0.1, qPrintable(QString::number(swings[1])));
}

void TestAnalog::importedExamplesSimulate()
{
    requireEngine();
    const QString dir = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits");
    if (!QDir(dir).exists())
        QSKIP("CircuitMaker examples not available");
    int ran = 0, okCount = 0, skipped = 0;
    QStringList failures;
    for (QDirIterator it(dir, {QStringLiteral("*.ckt"), QStringLiteral("*.CKT")}, QDir::Files); it.hasNext();) {
        const QString path = it.next();
        // CM_EXAMPLE_ONLY=<file name>: just that one
        if (qEnvironmentVariableIsSet("CM_EXAMPLE_ONLY") && QFileInfo(path).fileName() != qEnvironmentVariable("CM_EXAMPLE_ONLY"))
            continue;
        Circuit c;
        CktImportReport report;
        QVERIFY(CktImporter::import(path, c, library(), &report, nullptr));
        if (!c.analysis().analogMode)
            continue;
        if (!report.unmapped.isEmpty()) {
            ++skipped;
            continue;
        }
        // the author's own note in the title block (Audioamp.ckt; CircuitMaker
        // itself misses its relay model)
        if (QFile f(path); f.open(QIODevice::ReadOnly) && f.read(4096).contains("Not for Simulation")) {
            ++skipped;
            continue;
        }
        // CM_EXAMPLE_OWN=1: the analysis the file sets up (Analyses Setup)
        if (!qEnvironmentVariableIsSet("CM_EXAMPLE_OWN")) {
            AnalysisSettings a = c.analysis();
            a.type = AnalysisSettings::Type::Transient;
            a.tranStop = QStringLiteral("2m");
            a.tranStep = QStringLiteral("2u");
            c.setAnalysis(a);
        }
        ++ran;
        spice::Deck deck;
        QElapsedTimer clock;
        clock.start();
        if (qEnvironmentVariableIsSet("CM_EXAMPLE_TIMES"))
            qInfo("%s ...", qPrintable(QFileInfo(path).fileName()));
        if (QFileInfo(path).fileName() == qEnvironmentVariable("CM_EXAMPLE_DECK")) {
            deck = spice::build(c, library(), NetlistBuilder::build(c, library()));
            qInfo("%s", qPrintable(deck.text));
        }
        const spice::Result r = simulate(c, &deck);
        if (qEnvironmentVariableIsSet("CM_EXAMPLE_TIMES"))
            qInfo("%s: %lld ms", qPrintable(QFileInfo(path).fileName()), qlonglong(clock.elapsed()));
        if (r.ok) ++okCount;
        else failures << QFileInfo(path).fileName() + QStringLiteral(": ") + r.error;
        // CM_EXAMPLE_DECK=<file name>: print that example's deck
        if (QFileInfo(path).fileName() == qEnvironmentVariable("CM_EXAMPLE_DECK"))
            qInfo("%s", qPrintable(deck.text));
    }
    qInfo("analog examples: %d simulated, %d ok, %d skipped (parts without equivalent, not for simulation)", ran, okCount, skipped);
    for (const QString& f : std::as_const(failures))
        qInfo("  %s", qPrintable(f));
    // a few demos depend on CircuitMaker's own convergence settings, or on
    // symbols that touch wires of another net after redrawing
    QVERIFY2(okCount >= ran * 0.85, qPrintable(failures.join(u'\n')));
}

QTEST_GUILESS_MAIN(TestAnalog)
#include "tst_analog.moc"
