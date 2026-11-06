#include "TestUtil.h"

#include "core/io/CktExporter.h"
#include "core/io/CktImporter.h"
#include "core/io/CmDeviceLib.h"
#include "core/io/CmUserLib.h"
#include "library/Macros.h"
#include "core/WireGeometry.h"

#include <QDirIterator>
#include <functional>
#include <QElapsedTimer>
#include <QRegularExpression>
#include "sim/analog/SpiceNetlister.h"
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
    void keypadExampleShowsKey();
    void rocketExample();
    void stepperExample();
    void ladderExample();
    void displayExample();
    void carsExample();
    void delayExample();
    void logicDisplayColours();
    void signalGenerators();
    void simExample();
    void importsProbes();
    void exportKeepsNodeNaming();
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
        if (qEnvironmentVariableIsSet("CM_LIST_MODES") && path.contains(u"Examples")) {
            QSet<QString> ids;
            for (const Component& comp : c.components()) ids << comp.symbolId;
            QStringList l(ids.cbegin(), ids.cend());
            l.sort();
            qInfo("%s %s: %s", c.analysis().analogMode ? "A" : "D", qPrintable(QFileInfo(path).fileName()), qPrintable(l.join(u' ')));
        }
        if (qEnvironmentVariableIsSet("CM_LIST_SPICE"))      // instances with a SPICE line of their own
            for (const Component& comp : c.components())
                if (comp.props.contains(QStringLiteral("cmSpice")))
                    qInfo("SPICE %s %s %s: %s", qPrintable(QFileInfo(path).fileName()), qPrintable(comp.symbolId), qPrintable(comp.designator),
                          qPrintable(comp.props.value(QStringLiteral("cmSpice")).toString()));
        if (qEnvironmentVariableIsSet("CM_NO_DIGITAL_MODEL") && !c.analysis().analogMode) {
            QSet<QString> none;
            for (const Component& comp : c.components()) {
                const SymbolDef* sd = library().find(comp.symbolId);
                if (sd && !sd->netLabel && sd->model.value(u"kind").toString().isEmpty()) none << comp.symbolId;
            }
            if (!none.isEmpty())
                qInfo("%s: no digital model: %s", qPrintable(QFileInfo(path).fileName()), qPrintable(QStringList(none.cbegin(), none.cend()).join(u' ')));
        }
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

// CircuitMaker's 4x4.ckt scans its key matrix and latches the key pressed
// into a hex display: every key shows its own digit.
void TestCktImport::keypadExampleShowsKey()
{
    const QString path = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits/4x4.ckt");
    if (!QFileInfo::exists(path))
        QSKIP("CircuitMaker examples not available");
    Circuit c;
    QVERIFY(CktImporter::import(path, c, library(), nullptr, nullptr));
    ItemId hex = 0, keys = 0;
    for (const Component& comp : c.components()) {
        if (comp.symbolId == u"hexdisplay") hex = comp.id;
        if (comp.symbolId == u"keypad4x4") keys = comp.id;
    }
    QVERIFY(hex && keys);
    Bench bench(c);
    QVERIFY2(bench.sim.warnings().isEmpty(), qPrintable(bench.sim.warnings().join(u'\n')));
    QStringList wrong;
    for (int k : {0, 5, 0xA, 0xF, 3, 0xC}) {
        bench.sim.interact(keys, QStringLiteral("key"), k);
        bench.sim.step(30000);
        if (bench.sim.componentState(hex) != k)
            wrong << QStringLiteral("%1 shows %2").arg(k, 0, 16).arg(bench.sim.componentState(hex), 0, 16);
        bench.sim.interact(keys, QStringLiteral("release"));
        bench.sim.step(500);
    }
    QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(u", ")));
}

// CircuitMaker's Rocket.ckt: Launch starts the count down from 9; the
// rocket lifts off at zero, and only when Arm/Safety is closed.
void TestCktImport::rocketExample()
{
    const QString path = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits/Rocket.ckt");
    if (!QFileInfo::exists(path))
        QSKIP("CircuitMaker examples not available");
    Circuit c;
    QVERIFY(CktImporter::import(path, c, library(), nullptr, nullptr));
    QVERIFY(!c.analysis().analogMode);          // CircuitMaker opens it in Digital Mode
    ItemId rocket = 0, arm = 0, launch = 0, counter = 0;
    for (const Component& comp : c.components()) {
        if (comp.symbolId == u"rocket") rocket = comp.id;
        else if (comp.symbolId == u"spst") arm = comp.id;
        else if (comp.symbolId == u"pb_no") launch = comp.id;
        else if (comp.symbolId == u"74168") counter = comp.id;
    }
    QVERIFY(rocket && arm && launch && counter);
    for (const bool armed : {false, true}) {
        Bench b(c);
        auto count = [&] {
            int v = 0;
            for (int k = 0; k < 4; ++k)
                v |= (b.value(c, counter, QStringLiteral("Q%1").arg(k)) == sim::Logic::L1) << k;
            return v;
        };
        b.sim.step(500);
        QCOMPARE(count(), 9);
        if (armed) b.sim.interact(arm, QStringLiteral("toggle"));
        b.sim.step(500);
        QCOMPARE(b.sim.componentState(rocket), 0);
        QList<int> counts{count()};
        int lifted = -1;
        b.sim.interact(launch, QStringLiteral("press"));
        for (int t = 0; t < 300 && lifted < 0; ++t) {
            if (t == 5) b.sim.interact(launch, QStringLiteral("release"));
            b.sim.step(1);
            if (counts.isEmpty() || counts.last() != count()) counts << count();
            if (b.sim.componentState(rocket) > 0) lifted = counts.last();
        }
        if (armed) {
            QCOMPARE(counts.mid(0, 10), (QList<int>{9, 8, 7, 6, 5, 4, 3, 2, 1, 0}));
            QCOMPARE(lifted, 0);
        } else {
            QVERIFY(counts.contains(0));
            QCOMPARE(lifted, -1);
        }
    }
}

// CircuitMaker's Stepper.ckt (Digital Mode): the sequencer and PROM step the
// motor through opto isolators, a full turn and back.
void TestCktImport::stepperExample()
{
    const QString path = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits/Stepper.ckt");
    if (!QFileInfo::exists(path))
        QSKIP("CircuitMaker examples not available");
    Circuit c;
    QVERIFY(CktImporter::import(path, c, library(), nullptr, nullptr));
    QVERIFY(!c.analysis().analogMode);
    ItemId motor = 0;
    for (const Component& comp : c.components())
        if (comp.symbolId == u"stepper") motor = comp.id;
    QVERIFY(motor);
    Bench b(c);
    QVERIFY2(b.sim.warnings().isEmpty(), qPrintable(b.sim.warnings().join(u'\n')));
    // a full turn one way, reversing at 45 degrees, as CircuitMaker shows it
    QList<int> steps{b.sim.componentState(motor)};
    for (int t = 0; t < 200; ++t) {
        b.sim.step(1);
        if (steps.last() != b.sim.componentState(motor)) steps << b.sim.componentState(motor);
    }
    QCOMPARE(steps.mid(0, 11), (QList<int>{1, 0, -1, -2, -3, -4, -5, -6, -7, -6, -5}));
}

// CircuitMaker's Ladder.ckt (Digital Mode): switches energize coils S1-S4,
// whose contacts run the motors, the lamp and relay K1.
void TestCktImport::ladderExample()
{
    const QString path = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits/Ladder.ckt");
    if (!QFileInfo::exists(path))
        QSKIP("CircuitMaker examples not available");
    Circuit c;
    QVERIFY(CktImporter::import(path, c, library(), nullptr, nullptr));
    QVERIFY(!c.analysis().analogMode);
    QHash<QString, ItemId> by;
    for (const Component& comp : c.components()) by.insert(comp.designator, comp.id);
    Bench b(c);
    QVERIFY2(b.sim.warnings().isEmpty(), qPrintable(b.sim.warnings().join(u'\n')));
    auto on = [&](const char* d) { return b.sim.componentState(by.value(QString::fromLatin1(d))); };
    auto settle = [&] { b.sim.step(20); };
    auto set = [&](const char* sw, int level) {
        if (on(sw) != level) b.sim.interact(by.value(QString::fromLatin1(sw)), QStringLiteral("toggle"));
        settle();
    };
    for (const char* sw : {"V1", "V2", "V3", "V4"}) set(sw, 0);
    settle();
    QCOMPARE(on("L1"), 0);
    QCOMPARE(on("M2"), 0);
    set("V1", 1);                       // switch S4
    QCOMPARE(on("L1"), 1);              // through contact S4
    QCOMPARE(on("M2"), 0);              // S1 or S3 still open
    set("V4", 1);                       // switch S1
    QCOMPARE(on("M2"), 1);              // S1, S2 (closed at rest), S4
    set("V3", 1);                       // switch S2 opens its NC contact
    QCOMPARE(on("M2"), 0);
    QCOMPARE(on("M1"), 0);
    b.sim.interact(by.value(QStringLiteral("S2")), QStringLiteral("press"));       // K1's coil
    settle();
    QCOMPARE(on("M1"), 1);
    b.sim.interact(by.value(QStringLiteral("S2")), QStringLiteral("release"));
    settle();
    QCOMPARE(on("M1"), 0);
    b.sim.interact(by.value(QStringLiteral("S1")), QStringLiteral("press"));
    settle();
    QCOMPARE(on("BZ1"), 1);
}

// CircuitMaker's Display.ckt: its "5V" switches open off; the Ascii Display
// keeps its text and cursor, and the Ascii Key types into it.
void TestCktImport::displayExample()
{
    const QString path = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits/Display.ckt");
    if (!QFileInfo::exists(path))
        QSKIP("CircuitMaker examples not available");
    Circuit c;
    QVERIFY(CktImporter::import(path, c, library(), nullptr, nullptr));
    ItemId term = 0, key = 0;
    for (const Component& comp : c.components()) {
        if (comp.symbolId == u"logicswitch") QCOMPARE(comp.props.value(QStringLiteral("state")).toInt(), 0);
        if (comp.symbolId == u"asciidisplay") term = comp.id;
        if (comp.symbolId == u"asciikey") key = comp.id;
    }
    QVERIFY(term && key);
    QCOMPARE(c.component(term)->props.value(QStringLiteral("text")).toString(),
             QStringLiteral("This is a test  of the LCD      display device..                "));
    QCOMPARE(c.component(term)->props.value(QStringLiteral("cursor")).toInt(), 48);
    Bench b(c);
    QVERIFY2(b.sim.warnings().isEmpty(), qPrintable(b.sim.warnings().join(u'\n')));
    auto type = [&](int ch) {
        b.sim.interact(key, QStringLiteral("key"), ch);
        b.sim.step(5);
        b.sim.interact(key, QStringLiteral("release"));
        b.sim.step(5);
    };
    auto line = [&](int row) {
        QString t;
        const QVector<int> mem = b.sim.memory(term);
        for (int i = row * 16; i < row * 16 + 16; ++i) t += QChar(mem.value(i));
        return t.trimmed();
    };
    b.sim.step(10);
    type('a');
    type('b');
    QCOMPARE(line(3), QStringLiteral("ab"));
    type(13);                           // a new line scrolls the text up
    type('c');
    QCOMPARE(line(0), QStringLiteral("of the LCD"));
    QCOMPARE(line(2), QStringLiteral("ab"));
    QCOMPARE(line(3), QStringLiteral("c"));
    type(8);                            // backspace erases it
    QCOMPARE(line(3), QString());
    // written back as CircuitMaker keeps it
    CmDeviceLib cmLib;
    QVERIFY(cmLib.load(CmDeviceLib::locate()));
    Circuit back;
    QVERIFY(CktImporter::importData(CktExporter::write(c, library(), cmLib), back, library(), nullptr, nullptr));
    for (const Component& comp : back.components())
        if (comp.symbolId == u"asciidisplay") {
            QCOMPARE(comp.props.value(QStringLiteral("text")).toString(), c.component(term)->props.value(QStringLiteral("text")).toString());
            QCOMPARE(comp.props.value(QStringLiteral("cursor")).toInt(), 48);
        }
}

// CircuitMaker's Cars.ckt: the race timers (User.lib's Timer macro) stay
// dark until Start, count while their car races and keep its time.
void TestCktImport::carsExample()
{
    const QString path = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits/Cars.ckt");
    if (!QFileInfo::exists(path))
        QSKIP("CircuitMaker examples not available");
    Circuit imported;
    QVERIFY(CktImporter::import(path, imported, library(), nullptr, nullptr));
    // colours and speeds, also written back
    CmDeviceLib cmLib;
    QVERIFY(cmLib.load(CmDeviceLib::locate()));
    Circuit back;
    QVERIFY(CktImporter::importData(CktExporter::write(imported, library(), cmLib), back, library(), nullptr, nullptr));
    for (const Circuit* cc : {&imported, &back}) {
        QStringList cars;
        for (const Component& comp : cc->components())
            if (comp.symbolId == u"car")
                cars << QStringLiteral("%1:%2:%3").arg(comp.pos.y()).arg(comp.props.value(QStringLiteral("color")).toString()).arg(comp.props.value(QStringLiteral("speed")).toInt());
        std::sort(cars.begin(), cars.end(), [](const QString& x, const QString& y) { return x.section(u':', 0, 0).toDouble() < y.section(u':', 0, 0).toDouble(); });
        QCOMPARE(QStringList({cars[0].section(u':', 1), cars[1].section(u':', 1), cars[2].section(u':', 1)}),
                 QStringList({QStringLiteral("red:4"), QStringLiteral("green:3"), QStringLiteral("blue:4")}));
    }
    QStringList warnings;
    const std::unique_ptr<Circuit> flat = macros::flatten(imported, library(), &warnings);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u"; ")));
    const Circuit& c = *flat;
    Bench b(c);
    QVERIFY2(b.sim.warnings().isEmpty(), qPrintable(b.sim.warnings().join(u'\n')));
    QVector<ItemId> digits, cars;
    ItemId start = 0;
    for (const Component& comp : c.components()) {
        if (comp.symbolId == u"seg7_ca") digits << comp.id;
        if (comp.symbolId == u"car") cars << comp.id;
        if (comp.symbolId == u"pb_no") start = comp.id;
    }
    QCOMPARE(digits.size(), 6);
    QVERIFY(start && cars.size() == 3);
    b.sim.step(200);
    for (ItemId d : digits) QCOMPARE(b.sim.componentState(d), 0);       // dark before the race
    b.sim.interact(start, QStringLiteral("press"));
    b.sim.step(20);
    b.sim.interact(start, QStringLiteral("release"));
    b.sim.step(3000);
    for (ItemId car : cars) QCOMPARE(b.sim.componentState(car), 100);  // all finished
    int shown = 0;
    for (ItemId d : digits) shown += b.sim.componentState(d) != 0;
    QCOMPARE(shown, 6);                                                // every timer shows its time
}

// CircuitMaker's Delay.ckt (User.lib's Delay macro): Start lights Q after
// the delay set on the hex keys; Reset puts it out.
void TestCktImport::delayExample()
{
    const QString path = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits/Delay.ckt");
    if (!QFileInfo::exists(path))
        QSKIP("CircuitMaker examples not available");
    Circuit imported;
    QVERIFY(CktImporter::import(path, imported, library(), nullptr, nullptr));
    QStringList warnings;
    const std::unique_ptr<Circuit> flat = macros::flatten(imported, library(), &warnings);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u"; ")));
    const Circuit& c = *flat;
    Bench b(c);
    QVERIFY2(b.sim.warnings().isEmpty(), qPrintable(b.sim.warnings().join(u'\n')));
    ItemId lamp = 0;
    QVector<ItemId> buttons;
    for (const Component& comp : c.components()) {
        if (comp.symbolId == u"logicdisplay") lamp = comp.id;
        if (comp.symbolId == u"pb_no") buttons << comp.id;
    }
    QVERIFY(lamp && buttons.size() == 2);
    std::sort(buttons.begin(), buttons.end(), [&](ItemId x, ItemId y) { return c.component(x)->pos.x() < c.component(y)->pos.x(); });
    const ItemId reset = buttons[0], start = buttons[1];   // Reset on the left, Start Delay on the right
    auto push = [&](ItemId pb) {
        b.sim.interact(pb, QStringLiteral("press"));
        b.sim.step(5);
        b.sim.interact(pb, QStringLiteral("release"));
    };
    b.sim.step(100);
    push(reset);
    b.sim.step(50);
    QCOMPARE(b.sim.componentState(lamp), 0);
    push(start);
    int litAt = -1;
    for (int t = 0; t < 20000 && litAt < 0; ++t) {
        b.sim.step(1);
        if (b.sim.componentState(lamp) == 1) litAt = t;
    }
    qInfo("Q lit %d ticks after Start", litAt);
    QVERIFY2(litAt >= 10 && litAt <= 60, "CircuitMaker lights it about 25 ticks after Start");
    push(reset);
    b.sim.step(50);
    QCOMPARE(b.sim.componentState(lamp), 0);
}

// Led.ckt's logic displays, left to right: red, green, blue, cyan, magenta, yellow.
void TestCktImport::logicDisplayColours()
{
    const QString path = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits/Led.ckt");
    if (!QFileInfo::exists(path))
        QSKIP("CircuitMaker examples not available");
    Circuit a;
    QVERIFY(CktImporter::import(path, a, library(), nullptr, nullptr));
    CmDeviceLib cmLib;
    QVERIFY(cmLib.load(CmDeviceLib::locate()));
    Circuit b;
    QVERIFY(CktImporter::importData(CktExporter::write(a, library(), cmLib), b, library(), nullptr, nullptr));
    for (const Circuit* c : {&a, &b}) {
        QList<std::pair<double, QString>> lamps;
        for (const Component& comp : c->components())
            if (comp.symbolId == u"logicdisplay") lamps << std::pair{comp.pos.x(), comp.props.value(QStringLiteral("color")).toString()};
        std::sort(lamps.begin(), lamps.end());
        QStringList colours;
        for (const auto& l : lamps) colours << l.second;
        QCOMPARE(colours, QStringList({QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("cyan"),
                                       QStringLiteral("magenta"), QStringLiteral("yellow")}));
    }
}

// Signal generators keep the waveform of their SPICE line, also written back.
void TestCktImport::signalGenerators()
{
    const QString dir = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits/");
    if (!QFileInfo::exists(dir + QStringLiteral("State.ckt")))
        QSKIP("CircuitMaker examples not available");
    CmDeviceLib cmLib;
    QVERIFY(cmLib.load(CmDeviceLib::locate()));
    auto gens = [&](const Circuit& c) {
        QStringList out;
        for (const Component& comp : c.components()) {
            if (comp.symbolId != u"siggen") continue;
            const QVariantMap& p = comp.props;
            auto v = [&](const char* k) { return QString::number(spice::parseNumber(p.value(QString::fromLatin1(k)).toString()).value_or(0), 'g', 6); };
            const QString w = p.value(QStringLiteral("wave")).toString();
            if (w == u"pulse")
                out << QStringLiteral("pulse %1 %2 %3 %4 %5 %6 %7").arg(v("v1"), v("v2"), v("delay"), v("rise"), v("fall"), v("width"), v("period"));
            else if (w == u"fm")
                out << QStringLiteral("fm %1 %2 %3 %4 %5").arg(v("offset"), v("amplitude"), v("carrier"), v("mdi"), v("signal"));
            else if (w == u"pwl") {
                QStringList pts;
                for (const QString& t : p.value(QStringLiteral("pwl")).toString().split(u' ', Qt::SkipEmptyParts))
                    pts << QString::number(spice::parseNumber(t).value_or(0), 'g', 6);
                out << QStringLiteral("pwl ") + pts.join(u' ');
            } else
                out << QStringLiteral("%1 %2 %3 %4").arg(w, v("offset"), v("amplitude"), v("frequency"));
        }
        out.sort();
        return out;
    };
    const QHash<QString, QStringList> expected{
        {QStringLiteral("State.ckt"), {QStringLiteral("pulse 0 5 1e-07 1e-08 1e-08 1e-07 1e-06")}},
        {QStringLiteral("Bandpass.ckt"), {QStringLiteral("pulse -1 1 0 1e-07 1e-07 0.0005 0.001")}},
        {QStringLiteral("Ammod.ckt"), {QStringLiteral("sine -0.5 0.3 1000"), QStringLiteral("sine 0 0.5 100000")}},
        {QStringLiteral("Switches.ckt"), {QStringLiteral("sine 0 1 1e+06"), QStringLiteral("sine 0 1.1 1e+06")}},
        // the SPICE line CircuitMaker simulates ("SIN(0 1 1k ...)", its own
        // netlist), not the 20 numbers of the label (100 mV)
        {QStringLiteral("Lpfilter.ckt"), {QStringLiteral("sine 0 1 1000")}},
        {QStringLiteral("Fcvs.ckt"), {QStringLiteral("fm 0 1 10000 5 1000")}},
        {QStringLiteral("Vco.ckt"), {QStringLiteral("pwl 0 0.1 0.002 0.3 0.004 1 0.006 1.5 0.008 0.5 0.01 2.5 0.012 4 0.014 0.1")}}};
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        Circuit a, b;
        QVERIFY(CktImporter::import(dir + it.key(), a, library(), nullptr, nullptr));
        QCOMPARE(gens(a), it.value());
        QVERIFY(CktImporter::importData(CktExporter::write(a, library(), cmLib), b, library(), nullptr, nullptr));
        QCOMPARE(gens(b), it.value());
    }
}

// CircuitMaker's Sim.ckt: its 74168 loads the open D inputs as 0 while PE's
// switch is off, and counts once it is on.
void TestCktImport::simExample()
{
    const QString path = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits/Sim.ckt");
    if (!QFileInfo::exists(path))
        QSKIP("CircuitMaker examples not available");
    Circuit imported;
    QVERIFY(CktImporter::import(path, imported, library(), nullptr, nullptr));
    QStringList warnings;
    const std::unique_ptr<Circuit> flat = macros::flatten(imported, library(), &warnings);
    const Circuit& c = *flat;
    Bench b(c);
    ItemId counter = 0, pe = 0;
    for (const Component& comp : c.components())
        if (comp.symbolId == u"74168") counter = comp.id;
    QVERIFY(counter);
    const int peNet = b.netlist.netOf(counter, library().find(QStringLiteral("74168"))->pinIndex(QStringLiteral("~PE")));
    for (const Component& comp : c.components())
        if (comp.symbolId == u"logicswitch" && b.netlist.netOf(comp.id, 0) == peNet) pe = comp.id;
    QVERIFY(pe);
    auto count = [&] {
        int v = 0;
        for (int k = 0; k < 4; ++k) v |= (b.value(c, counter, QStringLiteral("Q%1").arg(k)) == sim::Logic::L1) << k;
        return v;
    };
    b.sim.step(300);
    QCOMPARE(count(), 0);
    b.sim.interact(pe, QStringLiteral("toggle"));
    QSet<int> seen;
    for (int t = 0; t < 400; ++t) { b.sim.step(1); seen << count(); }
    QVERIFY2(seen.size() >= 5, "counts once PE is off");
}

// The waveform probes of the header land on the wires they marked.
void TestCktImport::importsProbes()
{
    const QString path = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits/Mixed93.ckt");
    if (!QFileInfo::exists(path))
        QSKIP("CircuitMaker examples not available");
    Circuit c;
    QVERIFY(CktImporter::import(path, c, library(), nullptr, nullptr));
    QCOMPARE(c.probes().size(), 5);                 // Clk, Q0..Q3
    const Netlist nl = NetlistBuilder::build(c, library());
    QSet<int> nets;
    for (QPointF p : c.probes())
        for (const Wire& w : c.wires())
            if (!w.bus && WireGeometry::onPolyline(p, w.points)) { nets << nl.wireNet.value(w.id); break; }
    QCOMPARE(nets.size(), 5);                       // each on a wire, all different nets
    // with their names
    QStringList names;
    for (QPointF p : c.probes()) names << c.probeName(p);
    QCOMPARE(names, (QStringList{QStringLiteral("Clk"), QStringLiteral("Q0"), QStringLiteral("Q1"), QStringLiteral("Q2"), QStringLiteral("Q3")}));
    // written back
    CmDeviceLib cmLib;
    QVERIFY(cmLib.load(CmDeviceLib::locate()));
    Circuit b;
    QVERIFY(CktImporter::importData(CktExporter::write(c, library(), cmLib), b, library(), nullptr, nullptr));
    QCOMPARE(b.probes().size(), 5);
    QCOMPARE(b.probeName(b.probes().first()), QStringLiteral("Clk"));

    // its 74112 halves drawn apart (U1A, U1B, U3A, U3B) are parts of two
    // packages: "65 0 0 <x> <parts> <part> <package> 0" after the package map
    const QString text = QString::fromLatin1(CktExporter::write(c, library(), cmLib)).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    static const QRegularExpression rec(QStringLiteral("\\n3 (U[13][AB])\\n(?:.*\\n){8,20}?65 0 0 \\d+ 2 (\\d) (\\d+) 0\\n"));
    QHash<QString, std::pair<int, int>> parts;
    for (auto it = rec.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        parts.insert(m.captured(1), {m.captured(2).toInt(), m.captured(3).toInt()});
    }
    QCOMPARE(parts.size(), 4);
    QCOMPARE(parts[QStringLiteral("U1A")].first, 1);
    QCOMPARE(parts[QStringLiteral("U1B")].first, 2);
    QCOMPARE(parts[QStringLiteral("U1A")].second, parts[QStringLiteral("U1B")].second);
    QCOMPARE(parts[QStringLiteral("U3A")].second, parts[QStringLiteral("U3B")].second);
    QVERIFY(parts[QStringLiteral("U1A")].second != parts[QStringLiteral("U3A")].second);
}

// 741.ckt written back so that CircuitMaker names its SPICE nodes as in the
// original: its BGND reads V(vcc_1), the node of Vcc's pin 1. CircuitMaker
// names a node after the earliest placed part on it, by the order stamp
// each part carries (a float written as the low half of a double); its
// suffix is the package pin (Q1_3: TO-18 "3,2,1").
void TestCktImport::exportKeepsNodeNaming()
{
    const QString path = QDir::homePath() + QStringLiteral("/.wine/drive_c/Program Files/CircuitMaker 2000/Examples/Circuits/741.ckt");
    if (!QFileInfo::exists(path))
        QSKIP("CircuitMaker examples not available");
    Circuit c;
    QVERIFY(CktImporter::import(path, c, library(), nullptr, nullptr));
    CmDeviceLib cmLib;
    QVERIFY(cmLib.load(CmDeviceLib::locate()));
    const QString text = QString::fromLatin1(CktExporter::write(c, library(), cmLib)).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    // the stamps: "2", then the date and the order, one part after another
    static const QRegularExpression stamp(QStringLiteral("\\n2\\n(\\S+) (\\S+)\\n"));
    QVector<float> order;
    for (auto it = stamp.globalMatch(text); it.hasNext();) {
        const double d = it.next().captured(2).toDouble();
        quint64 bits;
        std::memcpy(&bits, &d, 8);
        const quint32 low = quint32(bits);
        float f;
        std::memcpy(&f, &low, 4);
        order << f;
    }
    QVERIFY(order.size() > 40);
    // kept as the file had them: rising, about one apart
    for (int k = 0; k < order.size(); ++k) QVERIFY(std::abs(order[k] - float(k)) < 0.01f);
    // parts where CircuitMaker had them: a Device.lib pin is drawn from its
    // point, 6 (or 12, '+') out in its direction (+V's wire end is 12 below)
    // (within one unit: our grid rounds the wires)
    auto near = [&](int type, int x, int y) {
        const QRegularExpression at(QStringLiteral("\\n%1 (-?\\d+) (-?\\d+) 0 1 ").arg(type));
        for (auto it = at.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            if (std::abs(m.captured(1).toInt() - x) <= 1 && std::abs(m.captured(2).toInt() - y) <= 1) return true;
        }
        return false;
    };
    QVERIFY(near(167, 150, 257));
    QVERIFY(near(167, 425, 306));
    QVERIFY(near(168, 19, 346));
    // Q1's package pins as its model gives them
    QVERIFY(text.contains(QStringLiteral("\n2 Q1\n")));
    QVERIFY(text.contains(QStringLiteral("5 TO-18\n7\n0 3 2 1 3 2 1 0\n")));
    // the analyses as they were: operating point and transient, without
    // "Always set defaults", and the DC sweep's second source
    QVERIFY(text.contains(QStringLiteral("\n17 0 0\n0\n0\n3 Vin\n-1.5 -0.7 0.01\n3 Vcc\n10 14 1\n")));
    // its probe on GCM_2, as CircuitMaker names it
    const Netlist nl = NetlistBuilder::build(c, library());
    QCOMPARE(c.probes().size(), 1);
    QString probe;
    for (const Wire& w : c.wires())
        if (!w.bus && WireGeometry::onPolyline(c.probes().first(), w.points)) probe = NetlistBuilder::nodeName(nl, nl.wireNet.value(w.id), c, library());
    QCOMPARE(probe, QStringLiteral("GCM_2"));
    // Pcb.ckt names two nodes with texts on their wires (vin, vout)
    {
        Circuit pcb;
        QVERIFY(CktImporter::import(QFileInfo(path).dir().filePath(QStringLiteral("Pcb.ckt")), pcb, library(), nullptr, nullptr));
        QStringList named;
        for (const Net& n : NetlistBuilder::build(pcb, library()).nets)
            if (n.name == u"vin" || n.name == u"vout") named << n.name;
        named.sort();
        QCOMPARE(named, (QStringList{QStringLiteral("vin"), QStringLiteral("vout")}));
        const QString out = QString::fromLatin1(CktExporter::write(pcb, library(), cmLib)).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        QVERIFY(out.contains(QStringLiteral("\n0 32768 0 3\n")));
    }
    // Ps1.ckt's SAVE- parts are probes A, B, C. CircuitMaker plots A=Q1_1,
    // B=D1_3, C=V1_1; this file has no placing stamps, and between equals
    // CircuitMaker's pick (V1 over the earlier T1 on C) has no rule we know
    {
        Circuit ps;
        QVERIFY(CktImporter::import(QFileInfo(path).dir().filePath(QStringLiteral("Ps1.ckt")), ps, library(), nullptr, nullptr));
        const Netlist pnl = NetlistBuilder::build(ps, library());
        QStringList got;
        for (QPointF p : ps.probes()) {
            QString net;
            for (const Wire& w : ps.wires())
                if (!w.bus && WireGeometry::onPolyline(p, w.points)) { net = NetlistBuilder::nodeName(pnl, pnl.wireNet.value(w.id), ps, library()); break; }
            got << ps.probeName(p) + u'=' + net;
        }
        QCOMPARE(got.size(), 3);
        QCOMPARE(got.mid(0, 2), (QStringList{QStringLiteral("A=Q1_1"), QStringLiteral("B=D1_3")}));
        QVERIFY(got[2].startsWith(u"C="));
    }
    // a 5.5 file's "0 0 30 100 9" goes out as format 10 (5.6): with 9,
    // CircuitMaker reads the 5.6 file as 5.5 and opens nothing (Diffamp.ckt)
    {
        Circuit old;
        QVERIFY(CktImporter::import(QFileInfo(path).dir().filePath(QStringLiteral("Diffamp.ckt")), old, library(), nullptr, nullptr));
        const QString out = QString::fromLatin1(CktExporter::write(old, library(), cmLib)).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        QVERIFY(out.contains(QStringLiteral("\n0 0 30 100 10\n")));
    }
    // Analyses Setup: the AC sweep's kind (Lpfilter: decade) and the
    // transient's maximum step (State: 500n with a 100n step)
    {
        Circuit lp, st;
        QVERIFY(CktImporter::import(QFileInfo(path).dir().filePath(QStringLiteral("Lpfilter.ckt")), lp, library(), nullptr, nullptr));
        QVERIFY(CktImporter::import(QFileInfo(path).dir().filePath(QStringLiteral("State.ckt")), st, library(), nullptr, nullptr));
        QCOMPARE(lp.analysis().acSweep, QStringLiteral("dec"));
        QCOMPARE(st.analysis().tranMax.toDouble(), 5e-7);
        QVERIFY(QString::fromLatin1(CktExporter::write(lp, library(), cmLib)).contains(QStringLiteral("\r\n60 1 1 1e+07\r\n")));
        QVERIFY(QString::fromLatin1(CktExporter::write(st, library(), cmLib)).contains(QStringLiteral("\r\n0 1.5e-05 1e-07 5e-07\r\n")));
    }
    // the header's option bits and its Speed line
    QVERIFY(text.contains(QStringLiteral("\n9961472 0\n")));
    QVERIFY(text.contains(QStringLiteral("\n0 0 30 100 10\n")));
    auto exported = [&](const QString& name, Circuit& c) {
        if (!CktImporter::import(QFileInfo(path).dir().filePath(name), c, library(), nullptr, nullptr)) return QString();
        return QString::fromLatin1(CktExporter::write(c, library(), cmLib)).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    };
    // the transient from the initial conditions (Put: UIC), in the file and the deck
    {
        Circuit put;
        const QString out = exported(QStringLiteral("Put.ckt"), put);
        QVERIFY(put.analysis().tranUic);
        QVERIFY(out.contains(QStringLiteral("\n16 2 0\n")));
        QVERIFY(put.probes().size() == 2);
    }
    // a 4.0 file's SAVE- probes go back as 5.x SAVE- parts (CircuitMaker plots them)
    {
        Circuit ujt;
        const QString out = exported(QStringLiteral("Ujt.ckt"), ujt);
        QVERIFY(out.contains(QStringLiteral("5 SAVE-\n218 105 163 0 10 64\n")));
        QVERIFY(out.contains(QStringLiteral("\n12 *TRAN 0 5.76\n")));
    }
    // Input / Output that name their node keep their number (Vtpwramp: 64, NFB),
    // and parts that did not move go back where CircuitMaker had them (Diffamp's Q1)
    {
        Circuit vt, da;
        QVERIFY(exported(QStringLiteral("Vtpwramp.ckt"), vt).contains(QStringLiteral("\n6 Input~\n177 122 419 0 17 64\n")));
        const Netlist nl = NetlistBuilder::build(vt, library());
        bool named = false;
        for (int n = 0; n < nl.nets.size(); ++n) named |= NetlistBuilder::nodeName(nl, n, vt, library()) == u"NFB";
        QVERIFY(named);
        const QString diff = exported(QStringLiteral("Diffamp.ckt"), da);
        QVERIFY(diff.contains(QStringLiteral("\n219 199 110 0 3 ")));
        // texts where CircuitMaker had them, a generator's label as it writes
        // it, and a part turned about the middle of its drawing (the -12V
        // supply, upside down: 6 above where a turn about its origin puts it)
        QVERIFY(diff.contains(QStringLiteral("\n11 -100m/100mV\n-36 -48 41 -40\n")));
        QVERIFY(diff.contains(QStringLiteral("\n167 169 274 0 1 ")));
    }
    // a 4.0 file's texts (Ujt), a math block's expression as its third label
    // (Math1), a potentiometer's wiper in its label (Ammod), a Logic Switch's
    // own label (Digfault: "5V" while off), a generator's label out of date
    // in the file, kept while its waveform is (Cbamp)
    {
        Circuit u, m, a, d, cb;
        QVERIFY(exported(QStringLiteral("Ujt.ckt"), u).contains(QStringLiteral("\n3 330\n7 2 28 10\n")));
        QVERIFY(exported(QStringLiteral("Math1.ckt"), m).contains(QStringLiteral("\n2 M1\n-7 -36 7 -28\n4 -(V)\n-13 -14 15 -6\n")));
        QVERIFY(exported(QStringLiteral("Ammod.ckt"), a).contains(QStringLiteral("\n7 50k 80%\n-24 -20 25 -12\n")));
        QVERIFY(exported(QStringLiteral("Digfault.ckt"), d).contains(QStringLiteral("\n2 5V\n-7 -16 7 -8\n")));
        QVERIFY(exported(QStringLiteral("Cbamp.ckt"), cb).contains(QStringLiteral("\n5 -1/1V\n-15 -48 20 -40\n")));
    }
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
    QVERIFY2(same == files, qPrintable(different.join(u", ")));
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
    AnalysisSettings as;
    as.analogMode = true;
    as.type = AnalysisSettings::Type::AC;
    as.acStart = QStringLiteral("10");
    as.acStop = QStringLiteral("1Meg");
    as.acPointsPerDecade = 50;
    as.tranStop = QStringLiteral("5u");
    as.tranStep = QStringLiteral("25n");
    as.spiceOptions = QStringLiteral("METHOD=GEAR ITL4=100");
    a.setAnalysis(as);
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
    QCOMPARE(int(b.analysis().type), int(AnalysisSettings::Type::AC));
    QCOMPARE(b.analysis().acStop.toDouble(), 1e6);
    QCOMPARE(b.analysis().acPointsPerDecade, 50);
    QCOMPARE(b.analysis().tranStop.toDouble(), 5e-6);
    QCOMPARE(b.analysis().tranStep.toDouble(), 25e-9);
    QCOMPARE(b.analysis().spiceOptions, QStringLiteral("METHOD=GEAR ITL4=100"));
    QVERIFY(!b.titleBlock().visible);           // CircuitMaker prints it, it is not on the screen
    QVERIFY(data.contains("\r\n2\r\n8 Two keys"));
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
                  qsizetype(ours.size() - 1), qPrintable(bad.mid(0, qEnvironmentVariableIsSet("CM_PIN_DEVICE") ? bad.size() : 3).join(u"; ")));
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
