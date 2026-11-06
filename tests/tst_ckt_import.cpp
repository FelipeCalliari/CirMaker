#include "TestUtil.h"

#include "core/io/CktExporter.h"
#include "core/io/CktImporter.h"
#include "core/io/CmDeviceLib.h"
#include "core/io/CmUserLib.h"
#include "library/Macros.h"

#include <QDirIterator>
#include <functional>
#include <QElapsedTimer>
#include <QRegularExpression>
#include "sim/digital/EventSimulator.h"

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
    void digitalSweep();
    void symbolPinGeometry();
    void propagationDelaysRoundTrip();
    void keysAndTextsRoundTrip();
    void importsTitleBlock();
    void busEntriesAtOnePoint();

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
    // layouts). What remains is in the files themselves: stale net numbers
    // (Lab3-Tx: a STOPBIT terminal in two nets), a wire saved with net 0
    // (CPU23: resetINS to a PROM output) and wires CircuitMaker draws 1 unit
    // apart (Projeto G3).
    QVERIFY2(shorts <= 4, qPrintable(QString::number(shorts)));
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
            if (!CktExporter::deviceFor(*library().find(comp.symbolId), cmLib) && !CktExporter::macroFor(*library().find(comp.symbolId)))
                skipped << comp.id;
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
    // the ports reach distinct nets (two are named A1 in CircuitMaker's own macro)
    const Netlist nl = NetlistBuilder::build(inner, library());
    QSet<int> nets;
    QStringList names;
    for (const Component& comp : inner.components())
        if (comp.symbolId == u"port_in" || comp.symbolId == u"port_out") {
            nets.insert(nl.netOf(comp.id, 0));
            names << comp.label + u'=' + QString::number(nl.netOf(comp.id, 0));
        }
    QVERIFY2(nets.size() >= 10, qPrintable(names.join(u' ')));
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

// Buswire.ckt has entries "1" and "2" ending at one point of bus Seq: in
// CircuitMaker only names connect on a bus, so they stay apart.
void TestCktImport::busEntriesAtOnePoint()
{
    QString path;
    for (const QString& f : circuitFiles())
        if (f.endsWith(QStringLiteral("/Buswire.ckt"))) path = f;
    if (path.isEmpty())
        QSKIP("Buswire.ckt not available");
    Circuit c;
    CktImportReport report;
    QVERIFY(CktImporter::import(path, c, library(), &report, nullptr));
    QCOMPARE(report.shorts, 0);
    const Netlist nl = NetlistBuilder::build(c, library());
    auto net = [&](const QString& ref, const QString& pin) {
        for (const Component& comp : c.components())
            if (comp.designator == ref) return nl.netOf(comp.id, library().find(comp.symbolId)->pinIndex(pin));
        return -2;
    };
    QVERIFY(net(QStringLiteral("A13"), QStringLiteral("Q0")) != net(QStringLiteral("A13"), QStringLiteral("Q1")));
    QCOMPARE(net(QStringLiteral("A13"), QStringLiteral("Q0")), net(QStringLiteral("R12"), QStringLiteral("D0")));
    QCOMPARE(net(QStringLiteral("A13"), QStringLiteral("Q1")), net(QStringLiteral("R12"), QStringLiteral("D1")));
}

// The propagation delay is the device record's fourth number, less one
// (seen by setting a delay in CircuitMaker and saving).
void TestCktImport::propagationDelaysRoundTrip()
{
    const QString libPath = CmDeviceLib::locate();
    if (libPath.isEmpty())
        QSKIP("CircuitMaker 2000 is not installed");
    CmDeviceLib cmLib;
    QVERIFY(cmLib.load(libPath));
    Circuit a;
    const ItemId inv = place(a, QStringLiteral("inv"), {0, 0}, QStringLiteral("U1"));
    Component n = *a.component(inv);
    n.props.insert(QStringLiteral("delay"), 7);
    a.updateComponent(n);
    place(a, QStringLiteral("74161"), {300, 0}, QStringLiteral("U2"));
    const QByteArray data = CktExporter::write(a, library(), cmLib);
    Circuit b;
    QVERIFY(CktImporter::importData(data, b, library(), nullptr, nullptr));
    QHash<QString, int> delays;
    for (const Component& comp : b.components())
        delays.insert(comp.designator.left(2), comp.props.value(QStringLiteral("delay"), 1).toInt());
    QCOMPARE(delays.value(QStringLiteral("U1")), 7);
    QCOMPARE(delays.value(QStringLiteral("U2")), 1);
}

// Key values and free texts survive export: CircuitMaker reads the key's
// character from nets[11] and draws only as many characters as a text's
// record says.
void TestCktImport::keysAndTextsRoundTrip()
{
    const QString libPath = CmDeviceLib::locate();
    if (libPath.isEmpty())
        QSKIP("CircuitMaker 2000 is not installed");
    CmDeviceLib cmLib;
    QVERIFY(cmLib.load(libPath));
    Circuit a;
    const ItemId hex = place(a, QStringLiteral("hexkey"), {0, 0}, QStringLiteral("U1"));
    Component h = *a.component(hex);
    h.props.insert(QStringLiteral("value"), 11);
    a.updateComponent(h);
    const ItemId ascii = place(a, QStringLiteral("asciikey"), {300, 0}, QStringLiteral("U2"));
    Component k = *a.component(ascii);
    k.props.insert(QStringLiteral("value"), int('Q'));
    a.updateComponent(k);
    a.addText({0, {0, 300}, QStringLiteral("Start\nDelay"), 12});
    TitleBlock tb;
    tb.title = QStringLiteral("Two keys");
    tb.author = QStringLiteral("Someone");
    tb.date = QStringLiteral("2026-11-05");
    tb.revision = QStringLiteral("B");
    tb.visible = true;
    a.setTitleBlock(tb);
    const QByteArray data = CktExporter::write(a, library(), cmLib);
    QVERIFY2(data.contains("0 0 0 12\r\n"), "text length not recorded");
    Circuit b;
    QVERIFY(CktImporter::importData(data, b, library(), nullptr, nullptr));
    QHash<QString, int> values;
    for (const Component& comp : b.components())
        values.insert(comp.symbolId, comp.props.value(QStringLiteral("value"), -1).toInt());
    QCOMPARE(values.value(QStringLiteral("hexkey")), 11);
    QCOMPARE(values.value(QStringLiteral("asciikey")), int('Q'));
    QCOMPARE(b.texts().size(), 1);
    QCOMPARE(b.texts().first().text, QStringLiteral("Start\nDelay"));
    QCOMPARE(b.titleBlock().title, tb.title);
    QCOMPARE(b.titleBlock().author, tb.author);
    QCOMPARE(b.titleBlock().date, tb.date);
    QCOMPARE(b.titleBlock().revision, tb.revision);
    QVERIFY(b.titleBlock().visible);
}

// The title block CircuitMaker 2000 wrote (Options > Schematic > Title Block)
void TestCktImport::importsTitleBlock()
{
    const QByteArray head = "CircuitMaker Text\r\n5.6\r\nProbes: 0\r\n0 0 5 100 10\r\n42991616 0\r\n"
                            "2\r\n9 \r\r\nTITLEbb\r\n8 \r\r\nNAMEaa\r\n6 DATEee\r\n5 REVdd\r\n4 IDcc\r\n";
    Circuit a;
    place(a, QStringLiteral("inv"), {0, 0}, QStringLiteral("U1"));
    CmDeviceLib cmLib;
    if (!cmLib.load(CmDeviceLib::locate()))
        QSKIP("CircuitMaker 2000 is not installed");
    QByteArray data = CktExporter::write(a, library(), cmLib);
    // keep the device list and what follows; put CircuitMaker's header before it
    const int list = data.indexOf("\r\n1\r\n") + 2;
    QVERIFY(list > 2);
    data = head + data.mid(list);
    Circuit b;
    QVERIFY(CktImporter::importData(data, b, library(), nullptr, nullptr));
    QCOMPARE(b.components().size(), 1);
    QCOMPARE(b.titleBlock().title, QStringLiteral("TITLEbb"));
    QCOMPARE(b.titleBlock().author, QStringLiteral("NAMEaa"));
    QCOMPARE(b.titleBlock().date, QStringLiteral("DATEee"));
    QCOMPARE(b.titleBlock().revision, QStringLiteral("REVdd"));
    QVERIFY(b.titleBlock().visible);
}

// Diagnostic (CM_PIN_GEOMETRY=1): symbols whose pins are not where
// CircuitMaker's Device.lib puts them (relative to the first pin, at our scale).
void TestCktImport::symbolPinGeometry()
{
    if (qEnvironmentVariableIsEmpty("CM_PIN_GEOMETRY"))
        QSKIP("set CM_PIN_GEOMETRY=1");
    CmDeviceLib cmLib;
    QVERIFY(cmLib.load(CmDeviceLib::locate()));
    if (const QString dev = qEnvironmentVariable("CM_PIN_DEVICE"); !dev.isEmpty()) {
        // one Device.lib device: its pins, names and strings
        for (const CmLibDevice& d : cmLib.devices())
            if (d.name.contains(dev, Qt::CaseInsensitive)) {
                QStringList pins;
                for (int k = 0; k < d.pins.size(); ++k)
                    pins << QStringLiteral("%1:%2(%3,%4)").arg(k + 1).arg(d.pinNames.value(k)).arg(d.pins[k].x()).arg(d.pins[k].y());
                qInfo("%s type %d prefix %s value %s kind %s spice %s pkg %s | %s", qPrintable(d.name), d.type, qPrintable(d.prefix),
                      qPrintable(d.value), qPrintable(d.kind), qPrintable(d.spice), qPrintable(d.package), qPrintable(pins.join(u' ')));
            }
        return;
    }
    for (const SymbolDef* s : library().all()) {
        if (s->cmPins.isEmpty()) continue;
        const CmLibDevice* d = CktExporter::deviceFor(*s, cmLib);
        if (!d || d->pins.size() < 2) continue;
        QVector<QPointF> ours, theirs;
        QStringList names;
        for (int k = 0; k < s->cmPins.size() && k < d->pins.size(); ++k) {
            const int i = s->pinIndex(s->cmPins[k]);
            if (i < 0) continue;
            ours << s->pins[i].pos;
            theirs << QPointF(d->pins[k]) * (10.0 / 3.0);
            names << s->cmPins[k];
        }
        if (ours.size() < 2) continue;
        QStringList bad;
        for (int k = 1; k < ours.size(); ++k) {
            const QPointF a = ours[k] - ours[0], b = theirs[k] - theirs[0];
            if (QLineF(a, b).length() > 2)
                bad << QStringLiteral("%1 ours(%2,%3) cm(%4,%5)").arg(names[k]).arg(a.x()).arg(a.y()).arg(b.x()).arg(b.y());
        }
        if (!bad.isEmpty())
            qInfo("%s [%s]: %lld of %lld pins off: %s", qPrintable(s->id), qPrintable(d->name), qsizetype(bad.size()),
                  qsizetype(ours.size() - 1), qPrintable(bad.mid(0, 3).join(u"; ")));
    }
}

// Diagnostic (CM_DIGITAL_SWEEP=1): runs every circuit digitally and lists
// warnings, oscillation and nets left unknown. CM_SWEEP_ONLY=<file suffix>
// limits it to one circuit; with CM_SWEEP_NET=<net> it explains the first
// tick that net is unknown (or tick CM_SWEEP_AT), and CM_SWEEP_SHOW=<ticks>
// prints the displays every that many ticks.
void TestCktImport::digitalSweep()
{
    if (qEnvironmentVariableIsEmpty("CM_DIGITAL_SWEEP"))
        QSKIP("set CM_DIGITAL_SWEEP=1");
    QHash<QString, int> warningKinds;
    const QString only = qEnvironmentVariable("CM_SWEEP_ONLY"), traceNet = qEnvironmentVariable("CM_SWEEP_NET");
    for (const QString& path : circuitFiles()) {
        if (!only.isEmpty() && !path.endsWith(only)) continue;
        Circuit c;
        if (!CktImporter::import(path, c, library(), nullptr, nullptr)) continue;
        const Netlist nl = NetlistBuilder::build(c, library());
        sim::EventSimulator s;
        if (!s.build(c, library(), nl)) continue;
        if (!only.isEmpty())
            for (const Component& comp : c.components())
                if (comp.props.contains(QStringLiteral("data"))) {
                    const SymbolDef* sd = library().find(comp.symbolId);
                    QStringList pins;
                    for (int i = 0; i < sd->pins.size(); ++i) pins << sd->pins[i].name + u'=' + nl.nets[nl.netOf(comp.id, i)].name;
                    qInfo("%s %s data=%s pins %s", qPrintable(comp.designator), qPrintable(comp.symbolId),
                          qPrintable(comp.props.value(QStringLiteral("data")).toString().left(120)), qPrintable(pins.join(u' ')));
                }
        QElapsedTimer t; t.start();
        s.reset();
        if (!traceNet.isEmpty()) {
            // first tick a net goes unknown, with the inputs of the parts driving it
            int net = -1;
            for (int n = 0; n < nl.nets.size(); ++n) if (nl.nets[n].name == traceNet) net = n;
            auto show = [&](int n) { const sim::Logic l = s.netValue(n);
                return QString(l == sim::Logic::L0 ? u'0' : l == sim::Logic::L1 ? u'1' : l == sim::Logic::Z ? u'Z' : u'X'); };
            const int at = qEnvironmentVariableIntValue("CM_SWEEP_AT");
            QSet<int> seen;
            std::function<void(int, int)> explain = [&](int n, int depth) {
                if (depth > 3 || seen.contains(n)) return;
                seen.insert(n);
                for (const PinRef& r : nl.nets[n].pins) {
                    const Component* o = c.component(r.component);
                    const SymbolDef* os = library().find(o->symbolId);
                    if (os->pins[r.pin].dir == SymbolPin::Dir::In || os->netLabel) continue;
                    QStringList ins;
                    for (int i = 0; i < os->pins.size(); ++i)
                        if (os->pins[i].dir != SymbolPin::Dir::Out)
                            ins << os->pins[i].name + u'=' + show(nl.netOf(o->id, i)) + u'(' + nl.nets[nl.netOf(o->id, i)].name + u')';
                    qInfo("%*s%s %s drives %s=%s; inputs %s", depth * 2, "", qPrintable(o->designator), qPrintable(o->symbolId),
                          qPrintable(os->pins[r.pin].name), qPrintable(show(n)), qPrintable(ins.join(u' ')));
                    if (depth < 3)
                        for (int i = 0; i < os->pins.size(); ++i)
                            if (os->pins[i].dir != SymbolPin::Dir::Out && (at || s.netValue(nl.netOf(o->id, i)) == sim::Logic::X))
                                explain(nl.netOf(o->id, i), depth + 1);
                }
            };
            for (int t = 0; t < 5000; ++t) {
                if (at ? t == at : s.netValue(net) == sim::Logic::X) {
                    qInfo("%s X at tick %d", qPrintable(traceNet), t);
                    explain(net, 0);
                    break;
                }
                s.step(1);
            }
        }
        if (!only.isEmpty() && qEnvironmentVariableIsSet("CM_SWEEP_SHOW")) {
            for (int k = 0; k < 12; ++k) {
                s.step(qEnvironmentVariableIntValue("CM_SWEEP_SHOW"));
                QStringList shown;
                for (const Component& comp : c.components())
                    if (comp.symbolId.contains(u"display") || comp.symbolId.contains(u"seg"))
                        shown << comp.designator + u'=' + QString::number(s.componentState(comp.id), 16);
                qInfo("t=%llu %s", (unsigned long long)s.time(), qPrintable(shown.join(u' ')));
            }
        }
        s.step(5000);
        const qint64 ms = t.elapsed();
        int xs = 0;
        QStringList xnames;
        for (int n = 0; n < nl.nets.size(); ++n) {
            if (nl.nets[n].pins.size() < 2) continue;
            if (s.netValue(n) == sim::Logic::X) {
                ++xs;
                if (xnames.size() < 4) {
                    QStringList on;
                    for (const PinRef& r : nl.nets[n].pins) {
                        const Component* o = c.component(r.component);
                        const SymbolDef* os = library().find(o->symbolId);
                        if (!os->netLabel) on << o->designator + u'/' + o->symbolId + u'.' + os->pins[r.pin].name;
                    }
                    xnames << nl.nets[n].name + u'[' + on.join(u' ') + u']';
                }
            }
        }
        for (const QString& w : s.warnings()) {
            QString k = w; k.remove(QRegularExpression(QStringLiteral("^[^:]*: ")));
            warningKinds[k]++;
        }
        if (xs || s.oscillated() || !s.warnings().isEmpty() || ms > 300)
            qInfo("%s: X=%d %s osc=%d %lldms warn=%s", qPrintable(QFileInfo(path).fileName()), xs, qPrintable(xnames.join(u',')),
                  int(s.oscillated()), ms, qPrintable(s.warnings().mid(0, 3).join(u"; ")));
    }
    for (auto it = warningKinds.cbegin(); it != warningKinds.cend(); ++it)
        qInfo("warning x%d: %s", it.value(), qPrintable(it.key()));
}

QTEST_GUILESS_MAIN(TestCktImport)
#include "tst_ckt_import.moc"
