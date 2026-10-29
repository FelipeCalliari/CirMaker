#include "TestUtil.h"

using namespace cm;
using namespace cm::test;

class TestNetlist : public QObject {
    Q_OBJECT
private slots:
    void wireJoinsPins();
    void crossingWiresDoNotConnect();
    void teeJunctionConnects();
    void netLabelsJoin();
    void unconnectedPinsGetOwnNet();
};

void TestNetlist::wireJoinsPins()
{
    Circuit c;
    const ItemId a = place(c, QStringLiteral("and2"), {0, 0});
    const ItemId b = place(c, QStringLiteral("inv"), {100, 0});
    link(c, a, QStringLiteral("Y"), b, QStringLiteral("A"));
    const Netlist nl = NetlistBuilder::build(c, library());
    QCOMPARE(nl.netOf(a, 2), nl.netOf(b, 0));
    QVERIFY(nl.netOf(a, 0) != nl.netOf(a, 1));
}

void TestNetlist::crossingWiresDoNotConnect()
{
    Circuit c;
    c.addWire({0, {{0, 0}, {100, 0}}});
    c.addWire({0, {{50, -50}, {50, 50}}});
    const Netlist nl = NetlistBuilder::build(c, library());
    QCOMPARE(nl.nets.size(), 2);
    QVERIFY(nl.junctions.isEmpty());
}

void TestNetlist::teeJunctionConnects()
{
    Circuit c;
    c.addWire({0, {{0, 0}, {100, 0}}});
    c.addWire({0, {{50, 0}, {50, 50}}});
    const Netlist nl = NetlistBuilder::build(c, library());
    QCOMPARE(nl.nets.size(), 1);
    QCOMPARE(nl.junctions.size(), 1);
    QCOMPARE(nl.junctions.first(), QPointF(50, 0));
}

void TestNetlist::netLabelsJoin()
{
    Circuit c;
    const ItemId t1 = place(c, QStringLiteral("terminal"), {0, 0}, QStringLiteral("T1"), QStringLiteral("CLK"));
    const ItemId t2 = place(c, QStringLiteral("terminal"), {500, 500}, QStringLiteral("T2"), QStringLiteral("CLK"));
    const ItemId t3 = place(c, QStringLiteral("terminal"), {900, 0}, QStringLiteral("T3"), QStringLiteral("Q0"));
    const Netlist nl = NetlistBuilder::build(c, library());
    QCOMPARE(nl.netOf(t1, 0), nl.netOf(t2, 0));
    QVERIFY(nl.netOf(t1, 0) != nl.netOf(t3, 0));
    QCOMPARE(nl.nets[nl.netOf(t1, 0)].name, QStringLiteral("CLK"));
}

void TestNetlist::unconnectedPinsGetOwnNet()
{
    Circuit c;
    const ItemId a = place(c, QStringLiteral("and3"), {0, 0});
    const Netlist nl = NetlistBuilder::build(c, library());
    QCOMPARE(nl.nets.size(), 4);
    QVERIFY(nl.netOf(a, 3) >= 0);
}

QTEST_GUILESS_MAIN(TestNetlist)
#include "tst_netlist.moc"
