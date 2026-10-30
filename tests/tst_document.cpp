#include "TestUtil.h"

#include "core/Bom.h"
#include "core/PcbNetlist.h"
#include "core/UndoCommands.h"
#include "core/io/NativeFormat.h"

#include <QUndoStack>

using namespace cm;
using namespace cm::test;

class TestDocument : public QObject {
    Q_OBJECT
private slots:
    void bomGroupsAndSorts();
    void bomCsvQuotes();
    void titleBlockRoundTrip();
    void titleBlockUndo();
    void protelNetlist();
    void analysisRoundTrip();
    void packagedGates();
    void exclusionsAndFootprint();
    void pcbFormats();
};

void TestDocument::pcbFormats()
{
    Circuit c;
    const ItemId u = place(c, QStringLiteral("7408"), {0, 0}, QStringLiteral("U1"), QStringLiteral("7408"));
    const ItemId r = place(c, QStringLiteral("resistor"), {400, 0}, QStringLiteral("R1"), QStringLiteral("1k"));
    label(c, u, QStringLiteral("Y"), QStringLiteral("OUT"));
    label(c, r, QStringLiteral("1"), QStringLiteral("OUT"));
    const Netlist nl = NetlistBuilder::build(c, library());
    using F = PcbNetlist::Format;
    const QString tango = PcbNetlist::write(c, library(), nl, F::Tango);
    QVERIFY2(tango.contains(QStringLiteral("[\nR1\nresistor\n1k\n\n\n\n]")), qPrintable(tango));
    const QString pads = PcbNetlist::write(c, library(), nl, F::Pads);
    QVERIFY2(pads.startsWith(QStringLiteral("*PADS-PCB*\n*PART*\n")), qPrintable(pads));
    QVERIFY(pads.contains(QStringLiteral("U1 7408\n")));
    QVERIFY(pads.contains(QStringLiteral("*SIGNAL* OUT\nR1.1 U1.3\n")));
    QVERIFY(pads.endsWith(QStringLiteral("*END*\n")));
    const QString orcad = PcbNetlist::write(c, library(), nl, F::OrcadPcb2);
    QVERIFY2(orcad.contains(QStringLiteral(" ( 00000002 7408 U1 7408\n  ( 3 OUT )\n )")), qPrintable(orcad));
}

void TestDocument::exclusionsAndFootprint()
{
    Circuit c;
    const ItemId r1 = place(c, QStringLiteral("resistor"), {0, 0}, QStringLiteral("R1"), QStringLiteral("1k"));
    const ItemId r2 = place(c, QStringLiteral("resistor"), {400, 0}, QStringLiteral("R2"), QStringLiteral("1k"));
    Component a = *c.component(r1);
    a.props.insert(QStringLiteral("package"), QStringLiteral("AXIAL0.4"));
    a.props.insert(QStringLiteral("excludeBom"), true);
    c.updateComponent(a);
    Component b = *c.component(r2);
    b.props.insert(QStringLiteral("excludePcb"), true);
    c.updateComponent(b);
    const QVector<BomLine> bom = Bom::build(c, library());
    QCOMPARE(bom.size(), 1);
    QCOMPARE(bom.first().designators, QStringList{QStringLiteral("R2")});
    const QString net = PcbNetlist::write(c, library(), NetlistBuilder::build(c, library()));
    QVERIFY2(net.contains(QStringLiteral("[\nR1\nAXIAL0.4\n")), qPrintable(net));
    QVERIFY(!net.contains(QStringLiteral("R2")));
    // own pin numbers
    Component a2 = *c.component(r1);
    a2.props.insert(QStringLiteral("pins"), QStringLiteral("1=7,2=9"));
    a2.props.remove(QStringLiteral("excludeBom"));
    c.updateComponent(a2);
    const ItemId r3 = place(c, QStringLiteral("resistor"), {800, 0}, QStringLiteral("R3"), QStringLiteral("1k"));
    label(c, r1, QStringLiteral("2"), QStringLiteral("N"));
    label(c, r3, QStringLiteral("1"), QStringLiteral("N"));
    const QString net2 = PcbNetlist::write(c, library(), NetlistBuilder::build(c, library()));
    QVERIFY2(net2.contains(QStringLiteral("(\nN\nR1-9\nR3-1\n)")), qPrintable(net2));
}

void TestDocument::packagedGates()
{
    Circuit c;
    // two gates of U1 (parts A and C) and a generic 2-In AND in its 7408
    const ItemId a = place(c, QStringLiteral("7408"), {0, 0}, QStringLiteral("U1"), QStringLiteral("7408"));
    const ItemId b = place(c, QStringLiteral("7408"), {0, 300}, QStringLiteral("U1"), QStringLiteral("7408"));
    Component cb = *c.component(b);
    cb.props.insert(QStringLiteral("part"), 3);
    c.updateComponent(cb);
    place(c, QStringLiteral("and2"), {0, 600}, QStringLiteral("U2"));
    label(c, a, QStringLiteral("Y"), QStringLiteral("X1"));
    label(c, b, QStringLiteral("A"), QStringLiteral("X1"));
    const SymbolDef& s = *library().find(QStringLiteral("7408"));
    QCOMPARE(shownDesignator(s, *c.component(b)), QStringLiteral("U1C"));
    QCOMPARE(packagePin(s, *c.component(b), 0), QStringLiteral("9"));
    const QString net = PcbNetlist::write(c, library(), NetlistBuilder::build(c, library()));
    QCOMPARE(net.count(QStringLiteral("[\nU1\n")), 1);             // the package once
    QVERIFY2(net.contains(QStringLiteral("(\nX1\nU1-3\nU1-9\n)")), qPrintable(net));
    QVERIFY2(net.contains(QStringLiteral("[\nU2\n7408\n")), qPrintable(net));   // generic gate: its package
    const QVector<BomLine> bom = Bom::build(c, library());
    int packages = 0;
    for (const BomLine& l : bom) packages += l.quantity;
    QCOMPARE(packages, 2);
}

void TestDocument::analysisRoundTrip()
{
    Circuit c;
    AnalysisSettings a;
    a.analogMode = true;
    a.type = AnalysisSettings::Type::Noise;
    a.output = QStringLiteral("OUT");
    a.inputSource = QStringLiteral("V1");
    a.fourierFreq = QStringLiteral("50");
    a.fourierHarmonics = 12;
    a.sweep = AnalysisSettings::Sweep::MonteCarlo;
    a.mcRuns = 7;
    a.mcTolerance = QStringLiteral("5");
    a.paramDevice = QStringLiteral("R3");
    a.tempStep = QStringLiteral("10");
    a.breakEdge = false;
    c.setAnalysis(a);
    Circuit loaded;
    QVERIFY(NativeFormat::deserialize(NativeFormat::serialize(c), loaded, nullptr));
    QCOMPARE(loaded.analysis(), a);
}

void TestDocument::bomGroupsAndSorts()
{
    Circuit c;
    for (const char* ref : {"U10", "U2", "U1"})
        place(c, QStringLiteral("7408"), {0, 0}, QString::fromLatin1(ref), QStringLiteral("7408"));
    place(c, QStringLiteral("resistor"), {0, 0}, QStringLiteral("R1"), QStringLiteral("10k"));
    place(c, QStringLiteral("resistor"), {0, 0}, QStringLiteral("R2"), QStringLiteral("1k"));
    place(c, QStringLiteral("ground"), {0, 0}, QStringLiteral("GND1"));
    place(c, QStringLiteral("terminal"), {0, 0}, QStringLiteral("T1"), QStringLiteral("CLK"));
    place(c, QStringLiteral("netlabel"), {0, 0}, {}, QStringLiteral("D0"));

    const QVector<BomLine> bom = Bom::build(c, library());
    QCOMPARE(bom.size(), 3);                                  // power, terminal and label are not parts
    QCOMPARE(bom[0].designators, QStringList({QStringLiteral("R1")}));
    QCOMPARE(bom[2].quantity, 3);
    QCOMPARE(bom[2].designators, QStringList({QStringLiteral("U1"), QStringLiteral("U2"), QStringLiteral("U10")}));
}

void TestDocument::bomCsvQuotes()
{
    BomLine l;
    l.quantity = 2;
    l.designators = {QStringLiteral("U1"), QStringLiteral("U2")};
    l.device = QStringLiteral("Counter, \"fast\"");
    l.value = QStringLiteral("74163");
    const QString csv = Bom::toCsv({l});
    QCOMPARE(csv.section(u'\n', 1, 1), QStringLiteral("2,U1 U2,\"Counter, \"\"fast\"\"\",74163,"));
}

void TestDocument::titleBlockRoundTrip()
{
    Circuit c;
    TitleBlock tb;
    tb.title = QStringLiteral("Contador síncrono");
    tb.author = QStringLiteral("Calliari");
    tb.date = QStringLiteral("29/10/2026");
    tb.revision = QStringLiteral("B");
    tb.visible = true;
    tb.pos = {500, 300};
    c.setTitleBlock(tb);
    Circuit loaded;
    QVERIFY(NativeFormat::deserialize(NativeFormat::serialize(c), loaded, nullptr));
    QVERIFY(loaded.titleBlock() == tb);
}

void TestDocument::titleBlockUndo()
{
    Circuit c;
    QUndoStack undo;
    TitleBlock tb;
    tb.title = QStringLiteral("A");
    tb.visible = true;
    undo.push(new SetTitleBlockCommand(&c, tb, {}));
    QCOMPARE(c.titleBlock().title, QStringLiteral("A"));
    undo.undo();
    QVERIFY(!c.titleBlock().visible);
    QVERIFY(c.titleBlock().title.isEmpty());
}

void TestDocument::protelNetlist()
{
    Circuit c;
    const ItemId u = place(c, QStringLiteral("7408"), {0, 0}, QStringLiteral("U1"), QStringLiteral("7408"));
    const ItemId r = place(c, QStringLiteral("resistor"), {400, 0}, QStringLiteral("R1"), QStringLiteral("1k"));
    label(c, u, QStringLiteral("Y"), QStringLiteral("OUT"));
    label(c, r, QStringLiteral("1"), QStringLiteral("OUT"));
    const QString net = PcbNetlist::write(c, library(), NetlistBuilder::build(c, library()));
    QVERIFY2(net.contains(QStringLiteral("[\nU1\n7408\n7408\n]")), qPrintable(net));
    QVERIFY2(net.contains(QStringLiteral("(\nOUT\nR1-1\nU1-3\n)")), qPrintable(net));
    QVERIFY(!net.contains(QStringLiteral("T1")));                // labels are not parts
}

QTEST_GUILESS_MAIN(TestDocument)
#include "tst_document.moc"
