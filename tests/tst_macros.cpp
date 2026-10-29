#include "TestUtil.h"

#include "core/io/NativeFormat.h"
#include "library/Macros.h"

#include <QJsonDocument>

using namespace cm;
using namespace cm::test;
using sim::Logic;

namespace {

// Port device whose pin sits on another component's pin.
void port(Circuit& c, ItemId comp, const QString& pin, bool input, const QString& name)
{
    const QString id = input ? QStringLiteral("port_in") : QStringLiteral("port_out");
    place(c, id, pinPos(c, comp, pin) - library().find(id)->pins[0].pos, {}, name);
}

QJsonObject document(const Circuit& c) { return QJsonDocument::fromJson(NativeFormat::serialize(c)).object(); }

QString defineHalfAdder()
{
    Circuit inner;
    const ItemId x = place(inner, QStringLiteral("xor2"), {0, 0}, QStringLiteral("U1"));
    const ItemId a = place(inner, QStringLiteral("and2"), {0, 200}, QStringLiteral("U2"));
    port(inner, x, QStringLiteral("A"), true, QStringLiteral("A"));
    port(inner, x, QStringLiteral("B"), true, QStringLiteral("B"));
    label(inner, a, QStringLiteral("A"), QStringLiteral("A"));     // local terminals
    label(inner, a, QStringLiteral("B"), QStringLiteral("B"));
    port(inner, x, QStringLiteral("Y"), false, QStringLiteral("S"));
    port(inner, a, QStringLiteral("Y"), false, QStringLiteral("C"));
    QString error;
    const QString id = macros::registerMacro(QStringLiteral("Half Adder"), document(inner), &error);
    if (id.isEmpty()) qWarning("%s", qPrintable(error));
    return id;
}

QString defineFullAdder(const QString& half)
{
    Circuit inner;
    const ItemId h1 = place(inner, half, {0, 0}, QStringLiteral("U1"));
    const ItemId h2 = place(inner, half, {300, 0}, QStringLiteral("U2"));
    const ItemId o = place(inner, QStringLiteral("or2"), {600, 200}, QStringLiteral("U3"));
    port(inner, h1, QStringLiteral("A"), true, QStringLiteral("A"));
    port(inner, h1, QStringLiteral("B"), true, QStringLiteral("B"));
    label(inner, h1, QStringLiteral("S"), QStringLiteral("S1"));
    label(inner, h2, QStringLiteral("A"), QStringLiteral("S1"));
    port(inner, h2, QStringLiteral("B"), true, QStringLiteral("Cin"));
    port(inner, h2, QStringLiteral("S"), false, QStringLiteral("S"));
    label(inner, h1, QStringLiteral("C"), QStringLiteral("C1"));
    label(inner, h2, QStringLiteral("C"), QStringLiteral("C2"));
    label(inner, o, QStringLiteral("A"), QStringLiteral("C1"));
    label(inner, o, QStringLiteral("B"), QStringLiteral("C2"));
    port(inner, o, QStringLiteral("Y"), false, QStringLiteral("Cout"));
    return macros::registerMacro(QStringLiteral("Full Adder"), document(inner));
}

// Drives the instance's inputs with constants and returns its outputs.
QVector<Logic> evaluate(const QString& macro, const QVector<int>& inputs, const QStringList& outputs)
{
    const SymbolDef* s = library().find(macro);
    Circuit c;
    const ItemId m = place(c, macro, {0, 0}, QStringLiteral("M1"));
    int i = 0;
    for (const SymbolPin& p : s->pins)
        if (p.dir == SymbolPin::Dir::In)
            label(c, m, p.name, inputs.value(i++) ? QStringLiteral("HI") : QStringLiteral("LO"));
    const ItemId vcc = place(c, QStringLiteral("vcc"), {-2000, 0});
    const ItemId gnd = place(c, QStringLiteral("ground"), {-2000, 400});
    label(c, vcc, QStringLiteral("VCC"), QStringLiteral("HI"));
    label(c, gnd, QStringLiteral("GND"), QStringLiteral("LO"));
    QStringList warnings;
    const std::unique_ptr<Circuit> flat = macros::flatten(c, library(), &warnings);
    if (!flat || !warnings.isEmpty()) {
        qWarning("%s", qPrintable(warnings.join(u'\n')));
        return {};
    }
    Bench bench(*flat);
    if (!bench.sim.warnings().isEmpty()) qWarning("%s", qPrintable(bench.sim.warnings().join(u'\n')));
    bench.sim.step(20);
    QVector<Logic> out;
    for (const QString& o : outputs)
        out << bench.value(*flat, m, o);
    return out;
}

} // namespace

class TestMacros : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { library(); }
    void symbolFromPorts();
    void halfAdderSimulates();
    void nestedMacros();
    void documentCarriesDefinitions();
    void noMacrosNoCopy();
};

void TestMacros::symbolFromPorts()
{
    const QString id = defineHalfAdder();
    QCOMPARE(id, QStringLiteral("macro:Half Adder"));
    const SymbolDef* s = library().find(id);
    QVERIFY(s && macros::isMacro(*s));
    QCOMPARE(s->category, QStringLiteral("Macros"));
    QStringList pins;
    for (const SymbolPin& p : s->pins) pins << p.name;
    // inputs left top-down, then outputs right top-down; duplicate names count once
    QCOMPARE(pins, (QStringList{"A", "B", "S", "C"}));
    QVERIFY(s->pins[0].pos.x() < 0 && s->pins[2].pos.x() > 0);
    QVERIFY(s->pins[0].pos.y() < s->pins[1].pos.y());
    QCOMPARE(library().findByAlias(QStringLiteral("Half Adder")), s);

    Circuit empty;
    QString error;
    QVERIFY(macros::registerMacro(QStringLiteral("Nothing"), document(empty), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestMacros::halfAdderSimulates()
{
    const QString half = defineHalfAdder();
    for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 2; ++b) {
            const QVector<Logic> out = evaluate(half, {a, b}, {QStringLiteral("S"), QStringLiteral("C")});
            QCOMPARE(out.size(), 2);
            QCOMPARE(out[0], (a ^ b) ? Logic::L1 : Logic::L0);
            QCOMPARE(out[1], (a & b) ? Logic::L1 : Logic::L0);
        }
}

void TestMacros::nestedMacros()
{
    const QString full = defineFullAdder(defineHalfAdder());
    QVERIFY(!full.isEmpty());
    for (int v = 0; v < 8; ++v) {
        const int a = v & 1, b = (v >> 1) & 1, cin = v >> 2;
        const QVector<Logic> out = evaluate(full, {a, b, cin}, {QStringLiteral("S"), QStringLiteral("Cout")});
        QCOMPARE(out.size(), 2);
        const int sum = a + b + cin;
        QCOMPARE(out[0], (sum & 1) ? Logic::L1 : Logic::L0);
        QCOMPARE(out[1], (sum >> 1) ? Logic::L1 : Logic::L0);
    }
}

void TestMacros::documentCarriesDefinitions()
{
    const QString full = defineFullAdder(defineHalfAdder());
    Circuit c;
    place(c, full, {0, 0}, QStringLiteral("U1"));
    const QByteArray data = NativeFormat::serialize(c);
    library().removeSymbol(full);
    library().removeSymbol(QStringLiteral("macro:Half Adder"));
    QVERIFY(!library().find(full));

    Circuit loaded;
    QVERIFY(NativeFormat::deserialize(data, loaded, nullptr));
    QVERIFY(library().find(full));
    QVERIFY(library().find(QStringLiteral("macro:Half Adder")));     // nested definition came along
    QCOMPARE(evaluate(full, {1, 1, 1}, {QStringLiteral("S"), QStringLiteral("Cout")}),
             (QVector<Logic>{Logic::L1, Logic::L1}));
}

void TestMacros::noMacrosNoCopy()
{
    Circuit c;
    place(c, QStringLiteral("and2"), {0, 0});
    QVERIFY(!macros::flatten(c, library()));
}

QTEST_GUILESS_MAIN(TestMacros)
#include "tst_macros.moc"
