#include "core/NetlistBuilder.h"

#include "core/WireGeometry.h"
#include "library/LibraryRegistry.h"

#include <QMap>
#include <QLineF>

#include <algorithm>
#include <cmath>
#include <cstring>

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
    // node names (texts on a wire) name nets no label names
    for (const TextNote& t : circuit.texts()) {
        if (!t.nameAt || t.text.trimmed().isEmpty()) continue;
        const QPointF at = t.pos + *t.nameAt;
        for (const auto& [w, node] : wires)
            if (WireGeometry::onPolyline(at, w->points)) {
                Net& n = out.nets[netFor(node)];
                if (n.name.isEmpty()) n.name = t.text.trimmed();
                break;
            }
    }
    for (int i = 0; i < out.nets.size(); ++i)
        if (out.nets[i].name.isEmpty())
            out.nets[i].name = QStringLiteral("N%1").arg(i + 1);

    for (auto it = degree.cbegin(); it != degree.cend(); ++it)
        if (it.value() >= 3)
            out.junctions << where[it.key()];
    return out;
}

QString NetlistBuilder::nodeName(const Netlist& netlist, int net, const Circuit& circuit, const LibraryRegistry& library)
{
    if (net < 0 || net >= netlist.nets.size()) return {};
    const Net& n = netlist.nets[net];
    if (n.name != QStringLiteral("N%1").arg(net + 1)) return n.name;
    // the placing order: the second stamp a CircuitMaker file gave the part
    // (a float in a double's low half; none: 0), else after every such part
    auto order = [](const Component& c) -> double {
        const QVariant v = c.props.value(QStringLiteral("cmStamp"));
        if (!v.isValid()) return 1e30 + double(c.id);
        const QStringList st = v.toString().split(u' ', Qt::SkipEmptyParts);
        if (st.size() < 2) return 0;
        const double d = st[1].toDouble();
        if (std::abs(d) > 1e-300) return d;     // some files write the float itself ("36626.6 1")
        quint64 bits;
        std::memcpy(&bits, &d, 8);
        const quint32 low = quint32(bits);
        float f;
        std::memcpy(&f, &low, 4);
        return f;
    };
    const PinRef* first = nullptr;
    const Component* firstComp = nullptr;
    double firstOrder = 0;
    for (const PinRef& p : n.pins) {
        const Component* c = circuit.component(p.component);
        if (!c || c->designator.isEmpty()) continue;
        const double o = order(*c);
        // the earliest; between equals (files without stamps) the first in
        // the file, which is mostly what CircuitMaker picks
        if (!first || o < firstOrder || (o == firstOrder && (p.component < first->component
                                                              || (p.component == first->component && p.pin < first->pin))))
            first = &p, firstComp = c, firstOrder = o;
    }
    if (!first) return n.name;
    QString pin = QString::number(first->pin + 1);
    if (const SymbolDef* s = library.find(firstComp->symbolId); s && first->pin < s->pins.size()) {
        const QString name = s->pins[first->pin].name;
        const int cmPin = int(s->cmPins.indexOf(name));
        const QStringList kept = firstComp->props.value(QStringLiteral("cmPins")).toString().split(u' ', Qt::SkipEmptyParts);
        if (cmPin >= 0 && cmPin < kept.size()) pin = kept[cmPin];
        else if (!s->pins[first->pin].number.isEmpty()) pin = s->pins[first->pin].number;
        else if (cmPin >= 0) pin = QString::number(cmPin + 1);
    }
    return firstComp->designator + u'_' + pin;
}

} // namespace cm
