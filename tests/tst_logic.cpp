#include "TestUtil.h"

using namespace cm;
using namespace cm::test;
using sim::Logic;

class TestLogic : public QObject {
    Q_OBJECT
private slots:
    void libraryLoads();
    void gateTruthTables_data();
    void gateTruthTables();
    void floatingInputUsesPinDefault();
};

void TestLogic::libraryLoads()
{
    LibraryRegistry& lib = library();
    QVERIFY(lib.all().size() > 30);
    QVERIFY(lib.find(QStringLiteral("and2")));
    QCOMPARE(lib.findByAlias(QStringLiteral("2-In AND~"))->id, QStringLiteral("and2"));
    QCOMPARE(lib.findByAlias(QStringLiteral("4027"))->id, QStringLiteral("jkff_sr"));
    // generated gate pins sit on the 10-unit grid
    for (const SymbolDef* s : lib.all())
        for (const SymbolPin& p : s->pins) {
            QVERIFY2(std::fmod(p.pos.x(), 10) == 0 && std::fmod(p.pos.y(), 10) == 0,
                     qPrintable(s->id + u':' + p.name));
        }
}

void TestLogic::gateTruthTables_data()
{
    QTest::addColumn<QString>("symbol");
    QTest::addColumn<QString>("table");    // outputs for inputs 00, 01, 10, 11 (A is MSB)
    QTest::newRow("and2") << "and2" << "0001";
    QTest::newRow("nand2") << "nand2" << "1110";
    QTest::newRow("or2") << "or2" << "0111";
    QTest::newRow("nor2") << "nor2" << "1000";
    QTest::newRow("xor2") << "xor2" << "0110";
    QTest::newRow("xnor2") << "xnor2" << "1001";
}

void TestLogic::gateTruthTables()
{
    QFETCH(QString, symbol);
    QFETCH(QString, table);
    for (int in = 0; in < 4; ++in) {
        Circuit c;
        const ItemId g = place(c, symbol, {0, 0}, QStringLiteral("U1"));
        const ItemId a = place(c, QStringLiteral("logicswitch"), {-100, -50}, QStringLiteral("S1"));
        const ItemId b = place(c, QStringLiteral("logicswitch"), {-100, 50}, QStringLiteral("S2"));
        const ItemId out = place(c, QStringLiteral("logicdisplay"), {100, 0}, QStringLiteral("L1"));
        link(c, a, QStringLiteral("Y"), g, QStringLiteral("A"));
        link(c, b, QStringLiteral("Y"), g, QStringLiteral("B"));
        link(c, g, QStringLiteral("Y"), out, QStringLiteral("D0"));
        Bench bench(c);
        if (in & 2) bench.sim.interact(a, QStringLiteral("toggle"));
        if (in & 1) bench.sim.interact(b, QStringLiteral("toggle"));
        bench.sim.step(5);
        const Logic expected = table[in] == u'1' ? Logic::L1 : Logic::L0;
        QCOMPARE(bench.value(c, g, QStringLiteral("Y")), expected);
        QCOMPARE(bench.sim.componentState(out), int(expected == Logic::L1));
    }
}

void TestLogic::floatingInputUsesPinDefault()
{
    // 74x74 ~PRE/~CLR float high (TTL), so an unconnected flip-flop is not held reset
    Circuit c;
    const ItemId ff = place(c, QStringLiteral("dff_pc"), {0, 0}, QStringLiteral("U1"));
    const ItemId d = place(c, QStringLiteral("vcc"), {-100, -100});
    const ItemId clk = place(c, QStringLiteral("logicswitch"), {-100, 50}, QStringLiteral("S1"));
    link(c, d, QStringLiteral("VCC"), ff, QStringLiteral("D"));
    link(c, clk, QStringLiteral("Y"), ff, QStringLiteral("CLK"));
    Bench bench(c);
    QCOMPARE(bench.value(c, ff, QStringLiteral("Q")), Logic::L0);
    bench.sim.interact(clk, QStringLiteral("toggle"));
    bench.sim.step(3);
    QCOMPARE(bench.value(c, ff, QStringLiteral("Q")), Logic::L1);
    QCOMPARE(bench.value(c, ff, QStringLiteral("~Q")), Logic::L0);
}

QTEST_GUILESS_MAIN(TestLogic)
#include "tst_logic.moc"
