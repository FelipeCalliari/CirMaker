#include "TestUtil.h"

#include "core/io/CktImporter.h"

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

private:
    static QStringList circuitFiles();
};

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
    int devices = 0, wires = 0, labels = 0;
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

        // every imported model must build and run
        Bench bench(c);
        QVERIFY2(bench.sim.warnings().isEmpty(), qPrintable(path + u": " + bench.sim.warnings().join(u"; ")));
        bench.sim.step(500);
    }
    qInfo("%lld files, %d devices, %d wires, %d restoring net labels", qlonglong(files.size()), devices, wires, labels);
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

QTEST_GUILESS_MAIN(TestCktImport)
#include "tst_ckt_import.moc"
