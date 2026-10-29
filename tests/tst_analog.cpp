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
