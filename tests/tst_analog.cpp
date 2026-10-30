#include "TestUtil.h"

#include "sim/analog/NgspiceEngine.h"
#include "sim/analog/SpiceNetlister.h"
#include "core/io/CktImporter.h"

#include <QDirIterator>

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
        Circuit c;
        CktImportReport report;
        QVERIFY(CktImporter::import(path, c, library(), &report, nullptr));
        if (!c.analysis().analogMode)
            continue;
        if (!report.unmapped.isEmpty()) {
            ++skipped;
            continue;
        }
        AnalysisSettings a = c.analysis();
        a.type = AnalysisSettings::Type::Transient;
        a.tranStop = QStringLiteral("2m");
        a.tranStep = QStringLiteral("2u");
        c.setAnalysis(a);
        ++ran;
        const spice::Result r = simulate(c);
        if (r.ok) ++okCount;
        else failures << QFileInfo(path).fileName() + QStringLiteral(": ") + r.error;
    }
    qInfo("analog examples: %d simulated, %d ok, %d skipped (parts without equivalent)", ran, okCount, skipped);
    for (const QString& f : std::as_const(failures))
        qInfo("  %s", qPrintable(f));
    // a few demos depend on CircuitMaker's own convergence settings, or on
    // symbols that touch wires of another net after redrawing
    QVERIFY2(okCount >= ran * 0.85, qPrintable(failures.join(u'\n')));
}

QTEST_GUILESS_MAIN(TestAnalog)
#include "tst_analog.moc"
