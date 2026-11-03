#include "TestUtil.h"

#include "core/io/CktExporter.h"
#include "core/io/CktImporter.h"
#include "core/io/CmDeviceLib.h"
#include "core/io/CmUserLib.h"
#include "library/Macros.h"

#include <QDirIterator>

using namespace cm;
using namespace cm::test;

// Circuits are read from the directories in CM_CKT_DIRS (colon-separated),
// by default the CircuitMaker 2000 examples under Wine and the author's
// course circuits. Missing directories are skipped.
class TestCktImport : public QObject {
    Q_OBJECT
private slots:
    void rejectsOtherFiles();
    void importsAllCircuits();
    void importedCounterCounts();
    void importsDataSequencer();
    void challengeCounterStops();
    void importsPackageParts();
    void exportRoundTrip();
    void userLibMacros();
    void closedContacts();
    void memoryCircuitWritesAndReads();

private:
    static QStringList circuitFiles();
};

void TestCktImport::importsPackageParts()
{
    const QString file = QDir::homePath() + QStringLiteral("/Documents/github/_calliari_/tecdigi/CircuitMaker/FSM_0101_Moore.CKT");
    if (!QFile::exists(file))
        QSKIP("course circuit not available");
    Circuit c;
    QVERIFY(CktImporter::import(file, c, library(), nullptr, nullptr));
    QStringList shown;
    for (const Component& comp : c.components())
        if (comp.designator == u"U6") shown << shownDesignator(*library().find(comp.symbolId), comp);
    shown.sort();
    QCOMPARE(shown, (QStringList{"U6A", "U6B", "U6C"}));
}

QStringList TestCktImport::circuitFiles()
{
    QStringList dirs = qEnvironmentVariable("CM_CKT_DIRS").split(u':', Qt::SkipEmptyParts);
    if (dirs.isEmpty())
        dirs = {QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples"),
                QDir::homePath() + QStringLiteral("/Documents/github/_calliari_/tecdigi/CircuitMaker")};
    QStringList files;
    for (const QString& d : dirs)
        for (QDirIterator it(d, {QStringLiteral("*.ckt"), QStringLiteral("*.CKT")}, QDir::Files, QDirIterator::Subdirectories);
             it.hasNext();)
            files << it.next();
    return files;
}

void TestCktImport::rejectsOtherFiles()
{
    Circuit c;
    QString error;
    QVERIFY(!CktImporter::importData("hello", c, library(), nullptr, &error));
    QVERIFY(!error.isEmpty());
}

void TestCktImport::importsAllCircuits()
{
    const QStringList files = circuitFiles();
    if (files.isEmpty())
        QSKIP("no CircuitMaker circuits found");
    QMap<QString, int> unmapped;
    int devices = 0, wires = 0, labels = 0, shorts = 0;
    for (const QString& path : files) {
        Circuit c;
        CktImportReport report;
        QString error;
        QVERIFY2(CktImporter::import(path, c, library(), &report, &error), qPrintable(path + u": " + error));
        QVERIFY(report.components > 0);
        devices += report.components;
        wires += report.wires;
        for (auto it = report.unmapped.cbegin(); it != report.unmapped.cend(); ++it)
            unmapped[it.key()] += it.value();
        for (const Component& comp : c.components())
            labels += comp.symbolId == u"netlabel";
        shorts += report.shorts;
        if (report.shorts && qEnvironmentVariableIsSet("CM_CKT_SHORTS"))
            qInfo("%s: %d shorts (%s)", qPrintable(QFileInfo(path).fileName()), report.shorts, qPrintable(report.warnings.join(u"; ")));
        // every imported model must build and run
        Bench bench(c);
        QVERIFY2(bench.sim.warnings().isEmpty(), qPrintable(path + u": " + bench.sim.warnings().join(u"; ")));
        bench.sim.step(500);
    }
    qInfo("%lld files, %d devices, %d wires, %d net labels, %d nets merged by redrawn symbols",
          qlonglong(files.size()), devices, wires, labels, shorts);
    // regression guard for symbol geometry (138 before the CircuitMaker
    // layouts); what remains is stale net numbers in the files themselves and
    // wires CircuitMaker draws 1 unit apart (Vtpwramp's transformer, Projeto G3)
    QVERIFY2(shorts <= 8, qPrintable(QString::number(shorts)));
    QStringList missing;
    for (auto it = unmapped.cbegin(); it != unmapped.cend(); ++it)
        missing << QStringLiteral("%1 x%2").arg(it.key()).arg(it.value());
    qInfo("unmapped: %s", qPrintable(missing.join(QStringLiteral(", "))));
}

void TestCktImport::importedCounterCounts()
{
    QString path;
    for (const QString& f : circuitFiles())
        if (f.endsWith(QStringLiteral("/Contador_assincrono_JK_crescente.CKT")))
            path = f;
    if (path.isEmpty())
        QSKIP("Contador_assincrono_JK_crescente.CKT not available");

    Circuit c;
    CktImportReport report;
    QVERIFY(CktImporter::import(path, c, library(), &report, nullptr));
    QVERIFY2(report.unmapped.isEmpty(), qPrintable(report.unmapped.keys().join(u',')));

    ItemId hex = 0;
    for (const Component& comp : c.components())
        if (comp.symbolId == u"hexdisplay")
            hex = comp.id;
    QVERIFY(hex);
    Bench bench(c);
    QVERIFY2(bench.sim.warnings().isEmpty(), qPrintable(bench.sim.warnings().join(u'\n')));

    // a ripple counter passes through intermediate values while the carry
    // ripples; only values held for several ticks count as states
    QSet<int> seen;
    QVector<int> stable;
    int current = bench.sim.componentState(hex), held = 0;
    for (int i = 0; i < 20000 && stable.size() < 20; ++i) {
        bench.sim.step(1);
        const int v = bench.sim.componentState(hex);
        held = v == current ? held + 1 : 0;
        current = v;
        if (held == 8 && v >= 0 && (stable.isEmpty() || stable.last() != v)) {
            stable << v;
            seen.insert(v);
        }
    }
    int increments = 0;
    for (int i = 1; i < stable.size(); ++i)
        increments += stable[i] == (stable[i - 1] + 1) % 16;
    qInfo("stable sequence: %s", qPrintable([&] {
        QStringList l;
        for (int v : stable) l << QString::number(v, 16);
        return l.join(u' ');
    }()));
    QCOMPARE(seen.size(), 16);
    QCOMPARE(increments, int(stable.size()) - 1);
}

// Led.ckt: a pulser clocks a Data Sequencer (external clock) whose ten words
// walk a lit LED back and forth over six Logic Displays.
void TestCktImport::importsDataSequencer()
{
    QString path;
    for (const QString& f : circuitFiles())
        if (f.endsWith(QStringLiteral("/Examples/Circuits/Led.ckt")))
            path = f;
    if (path.isEmpty())
        QSKIP("Led.ckt not available");
    Circuit c;
    QVERIFY(CktImporter::import(path, c, library(), nullptr, nullptr));
    const Component* ds = nullptr;
    for (const Component& comp : c.components())
        if (comp.symbolId == u"dataseq")
            ds = &comp;
    QVERIFY(ds);
    QCOMPARE(ds->props.value(QStringLiteral("data")).toString(), QStringLiteral("01 02 04 08 10 20 10 08 04 02"));
    QCOMPARE(ds->props.value(QStringLiteral("lines")).toInt(), 10);
    QCOMPARE(ds->props.value(QStringLiteral("start")).toInt(), 0);
    QCOMPARE(ds->props.value(QStringLiteral("stop")).toInt(), 9);
    QCOMPARE(ds->props.value(QStringLiteral("external")).toInt(), 1);

    Bench bench(c);
    QVERIFY2(bench.sim.warnings().isEmpty(), qPrintable(bench.sim.warnings().join(u'\n')));
    QSet<int> addresses;
    for (int i = 0; i < 2000; ++i) {
        bench.sim.step(1);
        addresses.insert(bench.sim.componentState(ds->id));
    }
    QCOMPARE(addresses.size(), 10);
    // exactly one display lit at a time
    int lit = 0;
    for (const Component& comp : c.components())
        if (comp.symbolId == u"logicdisplay")
            lit += bench.sim.componentState(comp.id) == 1;
    QCOMPARE(lit, 1);
}

// Contador_desafio_7493_0_45_stop: two 7493 count 0..45 on two hex
// displays; reaching 46 resets them.
void TestCktImport::challengeCounterStops()
{
    QString path;
    for (const QString& f : circuitFiles())
        if (f.endsWith(QStringLiteral("/Contador_desafio_7493_0_45_stop.CKT")))
            path = f;
    if (path.isEmpty())
        QSKIP("Contador_desafio_7493_0_45_stop.CKT not available");
    Circuit c;
    QVERIFY(CktImporter::import(path, c, library(), nullptr, nullptr));
    Bench bench(c);
    QVERIFY2(bench.sim.warnings().isEmpty(), qPrintable(bench.sim.warnings().join(u'\n')));
    int chips = 0;
    ItemId disp1 = 0, disp2 = 0;
    for (const Component& comp : c.components()) {
        if (comp.symbolId == u"7493") ++chips;
        if (comp.designator == u"DISP1") disp1 = comp.id;
        if (comp.designator == u"DISP2") disp2 = comp.id;
    }
    QCOMPARE(chips, 2);
    QVERIFY(disp1 && disp2);
    QSet<int> values;
    int last = -1;
    for (int t = 0; t < 40000; ++t) {
        bench.sim.step(1);
        const int hi = bench.sim.componentState(disp2), lo = bench.sim.componentState(disp1);
        const int v = hi < 0 || lo < 0 ? -1 : hi * 16 + lo;
        if (v >= 0) values.insert(v);
        last = v;
    }
    QVERIFY(last >= 0);
    // hex displays show every value 0..45 (0x2D); 46 resets at once
    for (int v = 0; v <= 0x2d; ++v)
        QVERIFY2(values.contains(v), qPrintable(QString::number(v)));
    for (int v : std::as_const(values))
        QVERIFY2(v <= 0x2e, qPrintable(QString::number(v)));
}

namespace {

// Which pins share a net, as sorted groups of "component#pin" keys; the
// components are numbered in order, skipping those not exported and net
// labels (on a re-import they stand for connections the redrawn symbols lost).
QStringList partition(const Circuit& c, const QSet<ItemId>& skip)
{
    const Netlist n = NetlistBuilder::build(c, library());
    QMap<int, QStringList> groups;
    int index = 0;
    for (const Component& comp : c.components()) {
        if (skip.contains(comp.id)) continue;
        const SymbolDef* s = library().find(comp.symbolId);
        if (s && s->netLabel) continue;               // labels: the connections they make count
        for (int p = 0; s && p < s->pins.size(); ++p)
            groups[n.netOf(comp.id, p)] << QStringLiteral("%1#%2").arg(index).arg(s->pins[p].name);
        ++index;
    }
    QStringList out;
    for (QStringList g : std::as_const(groups)) {
        if (g.size() < 2) continue;
        g.sort();
        out << g.join(u' ');
    }
    out.sort();
    return out;
}

} // namespace

// Import, write as .ckt with the installed Device.lib, read back: the same
// parts with the same connections. CM_CKT_EXPORT_DIR keeps the written files
// (to open them in CircuitMaker).
void TestCktImport::exportRoundTrip()
{
    const QString libPath = CmDeviceLib::locate();
    if (libPath.isEmpty())
        QSKIP("CircuitMaker 2000 is not installed");
    CmDeviceLib cmLib;
    QVERIFY(cmLib.load(libPath));
    QVERIFY2(cmLib.size() > 400, qPrintable(QString::number(cmLib.size())));
    QCOMPARE(cmLib.find(QStringLiteral("2-In NAND~"))->pinCount, 3);
    QCOMPARE(CktExporter::deviceFor(*library().find(QStringLiteral("7400")), cmLib)->name, QStringLiteral("2-In NAND~"));
    QCOMPARE(CktExporter::deviceFor(*library().find(QStringLiteral("74161")), cmLib)->name, QStringLiteral("74LS161"));

    const QString keep = qEnvironmentVariable("CM_CKT_EXPORT_DIR");
    int files = 0, same = 0;
    QStringList different;
    for (const QString& path : circuitFiles()) {
        Circuit a;
        if (!CktImporter::import(path, a, library(), nullptr, nullptr)) continue;
        CktExportReport rep;
        const QByteArray data = CktExporter::write(a, library(), cmLib, &rep);
        if (!keep.isEmpty()) {
            QFile out(QDir(keep).filePath(QFileInfo(path).fileName()));
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(data);
        }
        Circuit b;
        CktImportReport back;
        QString error;
        QVERIFY2(CktImporter::importData(data, b, library(), &back, &error), qPrintable(path + u": " + error));
        QSet<ItemId> skipped;
        for (const Component& comp : a.components())
            if (!CktExporter::deviceFor(*library().find(comp.symbolId), cmLib)) skipped << comp.id;
        QVERIFY2(back.components >= rep.devices,
                 qPrintable(QStringLiteral("%1: %2 read back, %3 written").arg(path).arg(back.components).arg(rep.devices)));
        ++files;
        if (partition(a, skipped) == partition(b, {})) ++same;
        else {
            different << QFileInfo(path).fileName();
            if (qEnvironmentVariableIsSet("CM_CKT_EXPORT_DIFF")) {
                qInfo("%s: %d shorts read back (%s); skipped %s", qPrintable(path), back.shorts,
                      qPrintable(back.warnings.join(u"; ")), qPrintable(rep.skipped.join(u", ")));
                const QStringList pa = partition(a, skipped), pb = partition(b, {});
                for (const QString& g : pa) if (!pb.contains(g)) qInfo("  - %s", qPrintable(g));
                for (const QString& g : pb) if (!pa.contains(g)) qInfo("  + %s", qPrintable(g));
            }
        }
    }
    qInfo("%d files written, %d with the same connections; differing: %s", files, same, qPrintable(different.join(u", ")));
}

// Macros of CircuitMaker's own User.lib: Delay's circuit and pins, and a
// circuit using it imports with the macro as one of ours.
void TestCktImport::userLibMacros()
{
    const QString path = CmUserLib::locate();
    if (path.isEmpty())
        QSKIP("CircuitMaker's User.lib not installed");
    CmUserLib lib;
    QVERIFY(lib.load(path));
    const CmMacro* delay = lib.find(QStringLiteral("Delay"));
    QVERIFY(delay);
    QCOMPARE(delay->pins.size(), 11);
    QCOMPARE(delay->pins.first(), QStringLiteral("B4"));
    QVERIFY(delay->pins.contains(QStringLiteral("A1_2")));          // a repeated name made unique
    QCOMPARE(delay->body.devices.size(), 12);
    QCOMPARE(delay->body.wires.size(), 40);
    QVERIFY(lib.find(QStringLiteral("Timer~")) && lib.find(QStringLiteral("Lock")));

    const QString file = QFileInfo(path).absolutePath() + QStringLiteral("/Examples/Circuits/Delay.ckt");
    if (!QFileInfo::exists(file)) return;
    Circuit c;
    CktImportReport report;
    QVERIFY(CktImporter::import(file, c, library(), &report, nullptr));
    QVERIFY2(!report.unmapped.contains(QStringLiteral("Delay")), "Delay not mapped");
    const SymbolDef* s = library().find(macros::idFor(QStringLiteral("Delay")));
    QVERIFY(s && macros::isMacro(*s));
    QCOMPARE(s->cmPins.size(), 11);
    Circuit inner;
    QVERIFY(macros::innerCircuit(*s, inner));
    int ports = 0;
    for (const Component& comp : inner.components()) ports += comp.symbolId == u"port_in" || comp.symbolId == u"port_out";
    QCOMPARE(ports, 11);
}

// CircuitMaker saves both sides of a shut window's contact as one net; the
// import must not wire them together.
void TestCktImport::closedContacts()
{
    const QString file = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits/Alarm.ckt");
    if (!QFileInfo::exists(file))
        QSKIP("CircuitMaker examples not available");
    Circuit c;
    CktImportReport report;
    QVERIFY(CktImporter::import(file, c, library(), &report, nullptr));
    QVERIFY(report.unmapped.isEmpty());
    const Netlist nl = NetlistBuilder::build(c, library());
    const SymbolDef* w = library().find(QStringLiteral("window"));
    QVERIFY(w);
    int windows = 0;
    for (const Component& comp : c.components()) {
        if (comp.symbolId != u"window") continue;
        ++windows;
        QVERIFY(nl.netOf(comp.id, w->pinIndex(QStringLiteral("C"))) != nl.netOf(comp.id, w->pinIndex(QStringLiteral("NC"))));
    }
    QCOMPARE(windows, 6);
}



// The course's 4K x 8 memory: four 1K RAMs on one data bus, a 74139
// selecting one by A10-A11, 74245s between the bus, the data keys and the
// displays. Values written to two chips read back intact: a deselected
// chip (or one being written) drives the bus with an X otherwise.
void TestCktImport::memoryCircuitWritesAndReads()
{
    const QString file = QDir::homePath() + QStringLiteral("/Documents/github/_calliari_/tecdigi/CircuitMaker/Memoria_4k8_v3.CKT");
    if (!QFile::exists(file))
        QSKIP("course circuit not available");
    Circuit c;
    QVERIFY(CktImporter::import(file, c, library(), nullptr, nullptr));
    QHash<QString, ItemId> id;
    for (const Component& comp : c.components()) id.insert(comp.designator, comp.id);
    Bench b(c);
    auto key = [&](const char* k, int v) {
        b.sim.interact(id.value(QString::fromLatin1(k)), QStringLiteral("key"), v);
        b.sim.step(5);
    };
    auto address = [&](int a) { key("KPD1", a >> 8 & 15); key("KPD2", a >> 4 & 15); key("KPD3", a & 15); };
    auto toggleRead = [&]() {
        b.sim.interact(id.value(QStringLiteral("READ")), QStringLiteral("toggle"));
        b.sim.step(10);
    };
    // the displays' side of U4; -1 when any bit is X or Z
    auto shown = [&]() {
        int v = 0;
        for (int i = 0; i < 8; ++i) {
            const sim::Logic l = b.value(c, id.value(QStringLiteral("U4")), QStringLiteral("B%1").arg(i));
            if (l != sim::Logic::L0 && l != sim::Logic::L1) return -1;
            v |= int(l == sim::Logic::L1) << i;
        }
        return v;
    };
    QCOMPARE(int(b.value(c, id.value(QStringLiteral("READ")), QStringLiteral("Y"))), int(sim::Logic::L1));   // starts reading
    auto write = [&](int a, int v) {
        address(a);
        key("KPD4", v >> 4);
        key("KPD5", v & 15);
        toggleRead();                   // write
        QCOMPARE(shown(), v);
        toggleRead();                   // back to reading
    };
    write(0x123, 0x5a);                 // U0
    write(0x523, 0xc3);                 // U1: same low address, other chip
    write(0xfff, 0x81);                 // U3
    address(0x123);
    QCOMPARE(shown(), 0x5a);
    address(0x523);
    QCOMPARE(shown(), 0xc3);
    address(0xfff);
    QCOMPARE(shown(), 0x81);
    address(0x923);                     // U2, never written
    QCOMPARE(shown(), 0x00);
    address(0x124);
    QCOMPARE(shown(), 0x00);
}

QTEST_GUILESS_MAIN(TestCktImport)
#include "tst_ckt_import.moc"
