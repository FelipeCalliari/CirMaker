#include "TestUtil.h"

#include "core/WireRouter.h"

using namespace cm;
using namespace cm::test;

class TestNetlist : public QObject {
    Q_OBJECT
private slots:
    void routesAroundDevices();
    void wireJoinsPins();
    void crossingWiresDoNotConnect();
    void teeJunctionConnects();
    void netLabelsJoin();
    void unconnectedPinsGetOwnNet();
    void busLinesDoNotConnect();
    void busEntriesJoinByName();
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

void TestNetlist::busLinesDoNotConnect()
{
    // two wires ending on the same bus line stay separate
    Circuit c;
    c.addWire({0, {{0, 0}, {0, 400}}, true});
    c.addWire({0, {{-100, 100}, {0, 100}}});
    c.addWire({0, {{-100, 300}, {0, 300}}});
    const Netlist nl = NetlistBuilder::build(c, library());
    QCOMPARE(nl.nets.size(), 2);
    QVERIFY(nl.junctions.isEmpty());
}

void TestNetlist::busEntriesJoinByName()
{
    Circuit c;
    c.addWire({0, {{0, 0}, {0, 400}}, true});
    const ItemId w1 = c.addWire({0, {{-100, 100}, {0, 100}}});
    const ItemId w2 = c.addWire({0, {{100, 300}, {0, 300}}});
    const ItemId w3 = c.addWire({0, {{-100, 200}, {0, 200}}});
    place(c, QStringLiteral("netlabel"), {0, 100}, {}, QStringLiteral("D0"));
    place(c, QStringLiteral("netlabel"), {0, 300}, {}, QStringLiteral("D0"));
    place(c, QStringLiteral("netlabel"), {0, 200}, {}, QStringLiteral("D1"));
    const Netlist nl = NetlistBuilder::build(c, library());
    QCOMPARE(nl.wireNet[w1], nl.wireNet[w2]);
    QVERIFY(nl.wireNet[w1] != nl.wireNet[w3]);
}

void TestNetlist::routesAroundDevices()
{
    RouteObstacles o;
    o.bodies << QRectF(40, -40, 80, 80);                  // a device between the ends
    o.wires << QLineF(QPointF(0, -100), QPointF(0, 100)); // a vertical wire through the start's column
    o.pins << QPointF(160, 0) + QPointF(0, 10);
    const QVector<QPointF> r = routeWire({0, 0}, {200, 0}, o, 10);
    QVERIFY(r.size() >= 4);
    QCOMPARE(r.first(), QPointF(0, 0));
    QCOMPARE(r.last(), QPointF(200, 0));
    for (int i = 0; i + 1 < r.size(); ++i) {
        const QLineF seg(r[i], r[i + 1]);
        QVERIFY(qFuzzyCompare(seg.x1(), seg.x2()) || qFuzzyCompare(seg.y1(), seg.y2()));     // orthogonal
        for (double t = 0; t <= 1; t += 0.05) {
            const QPointF p = seg.pointAt(t);
            QVERIFY2(!o.bodies[0].adjusted(1, 1, -1, -1).contains(p), "enters the device");
            QVERIFY(p != o.pins[0]);
        }
        if (i > 0) QVERIFY2(!qFuzzyIsNull(r[i].x()), "bends on the wire");
        QVERIFY2(!(qFuzzyIsNull(seg.x1()) && qFuzzyIsNull(seg.x2())), "runs along the wire");
    }
    // nothing in the way: one straight segment
    QCOMPARE(routeWire({0, 0}, {0, 300}, {}, 10), (QVector<QPointF>{{0, 0}, {0, 300}}));
}

QTEST_GUILESS_MAIN(TestNetlist)
#include "tst_netlist.moc"
