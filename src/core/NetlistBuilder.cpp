#include "core/NetlistBuilder.h"

#include "core/WireGeometry.h"
#include "library/LibraryRegistry.h"

#include <QMap>
#include <QLineF>

#include <algorithm>

namespace cm {

namespace {

class UnionFind {
public:
    int add()
    {
        m_parent.push_back(int(m_parent.size()));
        return int(m_parent.size()) - 1;
    }
    int find(int x)
    {
        while (m_parent[x] != x)
            x = m_parent[x] = m_parent[m_parent[x]];
        return x;
    }
    void unite(int a, int b) { m_parent[find(a)] = find(b); }

private:
    std::vector<int> m_parent;
};

constexpr double kTolerance = 0.5;

bool onWire(QPointF p, const Wire& w) { return WireGeometry::onPolyline(p, w.points); }

struct PointKey {
    qint64 x, y;
    friend bool operator<(const PointKey& a, const PointKey& b) { return a.x != b.x ? a.x < b.x : a.y < b.y; }
};

PointKey key(QPointF p) { return {qRound64(p.x()), qRound64(p.y())}; }

} // namespace

Netlist NetlistBuilder::build(const Circuit& circuit, const LibraryRegistry& library)
{
    UnionFind uf;
    struct PinNode { PinRef ref; QPointF pos; int node; };
    QVector<PinNode> pins;
    QVector<std::pair<const Wire*, int>> wires;

    for (const Component& c : circuit.components()) {
        const SymbolDef* s = library.find(c.symbolId);
        if (!s)
            continue;
        for (int i = 0; i < s->pins.size(); ++i)
            pins.push_back({{c.id, i}, library.pinScenePos(c, i), uf.add()});
    }
    for (const Wire& w : circuit.wires())
        if (!w.points.isEmpty() && !w.bus)      // bus lines connect by entry labels only
            wires.push_back({&w, uf.add()});

    // degree per point, for junction dots: wire ends and pins count once, a
    // wire passing through an end point counts twice (it continues both ways)
    QMap<PointKey, int> degree;
    QMap<PointKey, QPointF> where;
    auto touch = [&](QPointF p, int count) {
        const PointKey k = key(p);
        degree[k] += count;
        where[k] = p;
    };

    for (const PinNode& p : pins) {
        touch(p.pos, 1);
        for (const auto& [w, node] : wires)
            if (onWire(p.pos, *w))
                uf.unite(p.node, node);
    }
    for (int i = 0; i < pins.size(); ++i)
        for (int j = i + 1; j < pins.size(); ++j)
            if (QLineF(pins[i].pos, pins[j].pos).length() <= kTolerance)
                uf.unite(pins[i].node, pins[j].node);

    for (const auto& [w, node] : wires) {
        const QPointF ends[2] = {w->points.first(), w->points.last()};
        for (QPointF e : ends) {
            touch(e, 1);
            for (const auto& [v, vnode] : wires) {
                if (v == w || !onWire(e, *v))
                    continue;
                uf.unite(node, vnode);
                const bool atVEnd = QLineF(e, v->points.first()).length() <= kTolerance
                                 || QLineF(e, v->points.last()).length() <= kTolerance;
                if (!atVEnd)
                    touch(e, 2);
            }
        }
    }

    // net labels: equal label text joins. A bus entry's label belongs to its
    // bus: it joins only entries of the same name on buses of that name
    // (CircuitMaker), and its net is named "BUS.NAME".
    QHash<QString, int> labelNode;
    for (const PinNode& p : pins) {
        const Component* c = circuit.component(p.ref.component);
        const SymbolDef* s = library.find(c->symbolId);
        if (!s->netLabel || c->label.trimmed().isEmpty())
            continue;
        const QString bus = c->props.value(QStringLiteral("bus")).toString().trimmed();
        const QString label = bus.isEmpty() ? c->label.trimmed() : bus + u'.' + c->label.trimmed();
        auto it = labelNode.constFind(label);
        if (it == labelNode.cend())
            labelNode.insert(label, p.node);
        else
            uf.unite(p.node, *it);
    }

    Netlist out;
    QHash<int, int> rootNet;
    auto netFor = [&](int node) {
        const int root = uf.find(node);
        auto it = rootNet.constFind(root);
        if (it != rootNet.cend())
            return *it;
        const int idx = int(out.nets.size());
        out.nets.push_back({});
        rootNet.insert(root, idx);
        return idx;
    };
    for (const PinNode& p : pins) {
        const int net = netFor(p.node);
        out.nets[net].pins << p.ref;
        out.pinNet.insert(p.ref, net);
    }
    for (const auto& [w, node] : wires) {
        const int net = netFor(node);
        out.nets[net].wires << w->id;
        out.wireNet.insert(w->id, net);
    }
    for (auto it = labelNode.cbegin(); it != labelNode.cend(); ++it)
        out.nets[netFor(it.value())].name = it.key();
    for (int i = 0; i < out.nets.size(); ++i)
        if (out.nets[i].name.isEmpty())
            out.nets[i].name = QStringLiteral("N%1").arg(i + 1);

    for (auto it = degree.cbegin(); it != degree.cend(); ++it)
        if (it.value() >= 3)
            out.junctions << where[it.key()];
    return out;
}

} // namespace cm
