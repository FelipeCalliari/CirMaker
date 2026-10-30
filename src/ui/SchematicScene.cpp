#include "ui/SchematicScene.h"

#include "core/Faults.h"
#include "core/WireGeometry.h"
#include "core/io/NativeFormat.h"
#include "library/LibraryRegistry.h"
#include "library/Macros.h"
#include "sim/digital/EventSimulator.h"
#include "ui/ComponentItem.h"
#include "ui/Hotkeys.h"
#include "ui/DataSequencerDialog.h"
#include "ui/MemoryEditorDialog.h"
#include "ui/PropertiesDialog.h"
#include "ui/SymbolPainter.h"
#include "ui/TextNoteItem.h"
#include "ui/TitleBlockItem.h"
#include "ui/WaveformView.h"
#include "ui/WireItem.h"

#include <QApplication>
#include <QClipboard>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMimeData>
#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QUndoStack>

#include <cmath>

namespace cm {

using WireGeometry::near;
using WireGeometry::moveEnd;
using WireGeometry::simplify;

namespace {

const QColor kInk(0x10, 0x10, 0x30);
const QString kMime = QStringLiteral("application/x-cirmaker-qt");

Selection fromCircuit(const Circuit& c)
{
    Selection s;
    for (const Component& x : c.components()) s.components << x;
    for (const Wire& x : c.wires()) s.wires << x;
    for (const TextNote& x : c.texts()) s.texts << x;
    return s;
}

} // namespace

SchematicScene::SchematicScene(Circuit* circuit, QUndoStack* undo, const LibraryRegistry& library, QObject* parent)
    : QGraphicsScene(parent), m_circuit(circuit), m_undo(undo), m_library(library)
{
    setSceneRect(-2000, -1500, 6000, 4500);
    setItemIndexMethod(NoIndex);

    connect(circuit, &Circuit::componentAdded, this, &SchematicScene::onComponentAdded);
    connect(circuit, &Circuit::componentChanged, this, &SchematicScene::onComponentChanged);
    connect(circuit, &Circuit::componentRemoved, this, &SchematicScene::onComponentRemoved);
    connect(circuit, &Circuit::wireAdded, this, &SchematicScene::onWireAdded);
    connect(circuit, &Circuit::wireChanged, this, &SchematicScene::onWireChanged);
    connect(circuit, &Circuit::wireRemoved, this, &SchematicScene::onWireRemoved);
    connect(circuit, &Circuit::textAdded, this, &SchematicScene::onTextAdded);
    connect(circuit, &Circuit::textChanged, this, &SchematicScene::onTextChanged);
    connect(circuit, &Circuit::textRemoved, this, &SchematicScene::onTextRemoved);
    connect(circuit, &Circuit::cleared, this, &SchematicScene::onCleared);
    connect(circuit, &Circuit::titleBlockChanged, this, &SchematicScene::onTitleBlockChanged);
    connect(circuit, &Circuit::modified, this, &SchematicScene::scheduleNetlist);
    connect(circuit, &Circuit::modified, this, &SchematicScene::clearAnalogOverlay);
    connect(circuit, &Circuit::probesChanged, this, [this] { update(); });

    m_timer.setInterval(30);
    connect(&m_timer, &QTimer::timeout, this, &SchematicScene::advance);

    for (ItemId id : circuit->components().keys()) onComponentAdded(id);
    for (ItemId id : circuit->wires().keys()) onWireAdded(id);
    for (ItemId id : circuit->texts().keys()) onTextAdded(id);
    onTitleBlockChanged();
    rebuildNetlist();
}

SchematicScene::~SchematicScene() = default;

QPointF SchematicScene::snap(QPointF p)
{
    return {std::round(p.x() / kGrid) * kGrid, std::round(p.y() / kGrid) * kGrid};
}

QWidget* SchematicScene::dialogParent() const
{
    return views().isEmpty() ? nullptr : views().first()->window();
}

// ---------------------------------------------------------------- model sync

void SchematicScene::onComponentAdded(ItemId id)
{
    const Component* c = m_circuit->component(id);
    const SymbolDef* s = m_library.find(c->symbolId);
    if (!s) {
        qWarning("scene: component %s uses unknown symbol %s", qPrintable(c->designator), qPrintable(c->symbolId));
        return;
    }
    auto* item = new ComponentItem(*c, s);
    addItem(item);
    m_componentItems.insert(id, item);
}

void SchematicScene::onComponentChanged(ItemId id)
{
    if (ComponentItem* item = m_componentItems.value(id))
        item->sync(*m_circuit->component(id));
}

void SchematicScene::onComponentRemoved(ItemId id)
{
    delete m_componentItems.take(id);
}

void SchematicScene::onWireAdded(ItemId id)
{
    auto* item = new WireItem(*m_circuit->wire(id));
    addItem(item);
    m_wireItems.insert(id, item);
}

void SchematicScene::onWireChanged(ItemId id)
{
    if (WireItem* item = m_wireItems.value(id))
        item->sync(*m_circuit->wire(id));
}

void SchematicScene::onWireRemoved(ItemId id)
{
    delete m_wireItems.take(id);
}

void SchematicScene::onTextAdded(ItemId id)
{
    auto* item = new TextNoteItem(*m_circuit->text(id));
    addItem(item);
    m_textItems.insert(id, item);
}

void SchematicScene::onTextChanged(ItemId id)
{
    if (TextNoteItem* item = m_textItems.value(id))
        item->sync(*m_circuit->text(id));
}

void SchematicScene::onTextRemoved(ItemId id)
{
    delete m_textItems.take(id);
}

void SchematicScene::onCleared()
{
    stopSimulation();
    setTool(Tool::Select);
    qDeleteAll(m_componentItems);
    qDeleteAll(m_wireItems);
    qDeleteAll(m_textItems);
    m_componentItems.clear();
    m_wireItems.clear();
    m_textItems.clear();
    onTitleBlockChanged();
}

void SchematicScene::onTitleBlockChanged()
{
    const TitleBlock& tb = m_circuit->titleBlock();
    if (!tb.visible) {
        delete m_titleItem;
        m_titleItem = nullptr;
        return;
    }
    if (!m_titleItem) {
        m_titleItem = new TitleBlockItem(tb);
        addItem(m_titleItem);
    } else {
        m_titleItem->sync(tb);
    }
}

void SchematicScene::refreshSymbols()
{
    for (auto it = m_componentItems.cbegin(); it != m_componentItems.cend(); ++it)
        if (const Component* c = m_circuit->component(it.key()))
            it.value()->sync(*c);
    rebuildNetlist();
}

void SchematicScene::scheduleNetlist()
{
    if (m_netlistPending)
        return;
    m_netlistPending = true;
    QTimer::singleShot(0, this, &SchematicScene::rebuildNetlist);
}

SchematicScene::AnalogInput SchematicScene::analogInput() const
{
    AnalogInput in{faults::copy(simCircuit()), m_netlist};
    faults::apply(*in.circuit, in.netlist, m_library, true);
    return in;
}

void SchematicScene::rebuildNetlist()
{
    m_netlistPending = false;
    m_flatWarnings.clear();
    m_flat = macros::flatten(*m_circuit, m_library, &m_flatWarnings);
    const bool faulted = m_faulted != nullptr;
    m_faulted.reset();
    m_netlist = NetlistBuilder::build(simCircuit(), m_library);
    if (faulted && m_sim) {       // keep the running simulation's rewiring
        m_faulted = faults::copy(simCircuit());
        faults::apply(*m_faulted, m_netlist, m_library, false);
    }
    updateSceneRect();
    update();
}

// The default sheet, grown so there is room to scroll past every item.
void SchematicScene::updateSceneRect()
{
    QRectF r(-2000, -1500, 6000, 4500);
    for (QGraphicsItem* it : items())
        if (it != m_ghost && !it->parentItem())
            r |= it->sceneBoundingRect().adjusted(-1000, -1000, 1000, 1000);
    if (m_border)
        r |= contentsRect().adjusted(-200, -200, 200, 200);
    if (r != sceneRect())
        setSceneRect(r);
}

// ---------------------------------------------------------------- tools

void SchematicScene::setRestrictions(const QStringList& options)
{
    m_restrictions = options;
    if (restricted(QStringLiteral("trace")))
        m_trace = false;
    if (restricted(QStringLiteral("nodeNames")))
        m_showNodeNames = false;
    if ((m_tool == Tool::Wire || m_tool == Tool::Bus || m_tool == Tool::NetLabel || m_tool == Tool::Delete
         || m_tool == Tool::Place) && !options.isEmpty())
        setTool(Tool::Select);
    update();
}

void SchematicScene::setTool(Tool tool)
{
    if (((tool == Tool::Wire || tool == Tool::Bus || tool == Tool::NetLabel) && restricted(QStringLiteral("wire")))
        || (tool == Tool::Delete && restricted(QStringLiteral("delete"))))
        return;
    if (tool != Tool::Place && m_ghost)
        cancelPlace();
    if (!m_wirePoints.isEmpty())
        cancelWire();
    m_tool = tool;
    for (QGraphicsView* v : views())
        v->viewport()->setCursor(tool == Tool::Select ? Qt::ArrowCursor
                                 : tool == Tool::Text || tool == Tool::NetLabel ? Qt::IBeamCursor
                                 : tool == Tool::Probe ? Qt::PointingHandCursor
                                 : tool == Tool::Delete ? Qt::ForbiddenCursor
                                 : tool == Tool::Zoom ? Qt::SizeAllCursor : Qt::CrossCursor);
    emit toolChanged(tool);
}

void SchematicScene::beginPlace(const QString& symbolId)
{
    if (restricted(QStringLiteral("library")))
        return;
    const SymbolDef* s = m_library.find(symbolId);
    if (!s || simulating())
        return;
    cancelPlace();      // choosing another device while placing replaces the ghost
    setTool(Tool::Place);
    m_ghostComponent = Component{};
    m_ghostComponent.symbolId = symbolId;
    m_ghostComponent.label = s->defaultLabel;
    m_ghostComponent.pos = snap(m_cursor);
    m_ghost = new ComponentItem(m_ghostComponent, s);
    m_ghost->setZValue(100);
    addItem(m_ghost);
    emit statusMessage(tr("Click to place %1 · R rotate · M mirror · Shift keeps placing · Esc cancels").arg(s->name));
}

void SchematicScene::cancelPlace()
{
    delete m_ghost;
    m_ghost = nullptr;
}

void SchematicScene::placeGhost(QPointF scenePos)
{
    const SymbolDef* s = m_ghost->symbol();
    Component c = m_ghostComponent;
    c.pos = snap(scenePos);
    c.designator = m_circuit->nextDesignator(s->prefix);
    Selection sel;
    sel.components << c;
    m_undo->push(new AddItemsCommand(m_circuit, sel, tr("Place %1").arg(s->name)));
}

bool SchematicScene::pinAt(QPointF p, QPointF* pinPos) const
{
    for (const ComponentItem* item : m_componentItems) {
        const SymbolDef* s = item->symbol();
        for (int i = 0; i < s->pins.size(); ++i) {
            const QPointF pp = m_library.pinScenePos(item->component(), i);
            if (near(pp, p, 4)) {
                if (pinPos) *pinPos = pp;
                return true;
            }
        }
    }
    return false;
}

bool SchematicScene::isConnectionPoint(QPointF p) const
{
    if (pinAt(p, nullptr))
        return true;
    for (const WireItem* w : m_wireItems)
        if (WireGeometry::onPolyline(p, w->wire().points))
            return true;
    return false;
}

void SchematicScene::wireClick(QPointF p)
{
    if (m_wirePoints.isEmpty()) {
        m_wirePoints = {p};
        update();
        return;
    }
    const QPointF last = m_wirePoints.last();
    if (near(p, last))
        return;
    const QPointF corner(p.x(), last.y());
    if (!near(corner, last) && !near(corner, p))
        m_wirePoints << corner;
    m_wirePoints << p;
    if (!near(p, m_wirePoints.first()) && isConnectionPoint(p))
        finishWire();
    update();
}

void SchematicScene::finishWire()
{
    const QVector<QPointF> pts = simplify(m_wirePoints);
    const bool bus = m_drawingBus;
    m_wirePoints.clear();
    m_wireFromSelect = false;
    m_drawingBus = false;
    if (pts.size() < 2) {
        update();
        return;
    }
    // an ordinary wire ending on a bus line becomes a named bus entry: the
    // name connects it to every other entry and net label with that name
    QVector<std::pair<QPointF, QString>> entries;
    if (!bus) {
        for (QPointF end : {pts.first(), pts.last()}) {
            if (!onBus(end) || onConductor(end))
                continue;
            const QString name = askNetName(tr("Bus entry"), m_lastNetName);
            if (name.isEmpty())
                continue;
            entries << std::pair{end, name};
        }
    }
    m_undo->beginMacro(bus ? tr("Add bus") : tr("Add wire"));
    Selection sel;
    sel.wires << Wire{0, pts, bus};
    m_undo->push(new AddItemsCommand(m_circuit, sel, {}));
    for (const auto& [at, name] : entries) {
        Selection label;
        Component c;
        c.symbolId = QStringLiteral("netlabel");
        c.pos = at;
        c.label = name;
        label.components << c;
        m_undo->push(new AddItemsCommand(m_circuit, label, {}));
    }
    m_undo->endMacro();
    update();
}

bool SchematicScene::onBus(QPointF p) const
{
    for (const WireItem* w : m_wireItems)
        if (w->wire().bus && WireGeometry::onPolyline(p, w->wire().points))
            return true;
    return false;
}

bool SchematicScene::onConductor(QPointF p) const
{
    if (pinAt(p, nullptr))
        return true;
    for (const WireItem* w : m_wireItems)
        if (!w->wire().bus && WireGeometry::onPolyline(p, w->wire().points))
            return true;
    return false;
}

// Suggests the next name after `suggestion` ("D3" -> "D4", "7" -> "8").
QString SchematicScene::askNetName(const QString& title, const QString& suggestion)
{
    QString next = suggestion;
    static const QRegularExpression trailing(QStringLiteral("^(.*?)(\\d+)$"));
    if (const auto m = trailing.match(suggestion); m.hasMatch())
        next = m.captured(1) + QString::number(m.captured(2).toInt() + 1);
    bool ok = false;
    const QString name = QInputDialog::getText(dialogParent(), title, tr("Net name:"), QLineEdit::Normal, next, &ok).trimmed();
    if (!ok || name.isEmpty())
        return {};
    m_lastNetName = name;
    return name;
}

void SchematicScene::placeNetLabel(QPointF p)
{
    const QString name = askNetName(tr("Net label"), m_lastNetName);
    if (name.isEmpty())
        return;
    Selection sel;
    Component c;
    c.symbolId = QStringLiteral("netlabel");
    c.pos = p;
    c.label = name;
    sel.components << c;
    m_undo->push(new AddItemsCommand(m_circuit, sel, tr("Add net label")));
}

void SchematicScene::cancelWire()
{
    m_wirePoints.clear();
    m_wireFromSelect = false;
    update();
}

void SchematicScene::deleteAt(QPointF p, bool snipWire)
{
    if (restricted(QStringLiteral("delete")))
        return;
    for (QGraphicsItem* it : items(p)) {
        if (it == m_ghost) continue;
        Selection sel;
        if (auto* c = qgraphicsitem_cast<ComponentItem*>(it)) {
            sel.components << *m_circuit->component(c->id());
        } else if (auto* t = qgraphicsitem_cast<TextNoteItem*>(it)) {
            sel.texts << *m_circuit->text(t->id());
        } else if (auto* w = qgraphicsitem_cast<WireItem*>(it)) {
            const Wire wire = *m_circuit->wire(w->id());
            const int seg = segmentAt(wire.points, p);
            if (snipWire && seg >= 0 && wire.points.size() > 2) {
                // Shift: remove only the clicked segment, splitting the wire
                Selection before, after, added;
                before.wires << wire;
                const QVector<QPointF> head = wire.points.mid(0, seg + 1), tail = wire.points.mid(seg + 1);
                m_undo->beginMacro(tr("Snip wire"));
                m_undo->push(new RemoveItemsCommand(m_circuit, before, {}));
                if (head.size() >= 2) added.wires << Wire{0, head, wire.bus, wire.name};
                if (tail.size() >= 2) added.wires << Wire{0, tail, wire.bus, wire.name};
                if (!added.isEmpty()) m_undo->push(new AddItemsCommand(m_circuit, added, {}));
                m_undo->endMacro();
                return;
            }
            sel.wires << wire;
        } else {
            continue;
        }
        m_undo->push(new RemoveItemsCommand(m_circuit, sel, tr("Delete")));
        return;
    }
}

void SchematicScene::nudgeSelection(QPointF delta)
{
    if (restricted(QStringLiteral("edit")))
        return;
    const Selection before = selection();
    if (before.components.isEmpty())
        return;
    Selection b, a;
    for (const Component& c : before.components) {
        b.components << c;
        Component n = c;
        n.pos += delta;
        a.components << n;
    }
    m_undo->push(new ModifyItemsCommand(m_circuit, b, a, tr("Nudge")));
}

// CircuitMaker's Set Label Positions: designator and label back to the symbol's places.
void SchematicScene::resetLabelPositions()
{
    if (simulating() || restricted(QStringLiteral("edit")))
        return;
    Selection before, after;
    for (const Component& c : selection().components) {
        if (!c.designatorAt && !c.labelAt) continue;
        Component n = c;
        n.designatorAt.reset();
        n.labelAt.reset();
        before.components << c;
        after.components << n;
    }
    if (!after.isEmpty())
        m_undo->push(new ModifyItemsCommand(m_circuit, before, after, tr("Set label positions")));
}

void SchematicScene::straightenWires()
{
    if (restricted(QStringLiteral("edit")))
        return;
    if (simulating())
        return;
    Selection sel = selection();
    if (sel.wires.isEmpty())
        for (const Wire& w : m_circuit->wires()) sel.wires << w;
    Selection before, after;
    for (const Wire& w : std::as_const(sel.wires)) {
        // drop repeated and collinear points, then short zig-zags
        QVector<QPointF> pts = simplify(w.points);
        for (int i = 1; i + 2 < pts.size(); ++i) {
            const QLineF mid(pts[i], pts[i + 1]);
            if (mid.length() < 2 * kGrid) {
                // move the short middle segment onto the previous one's line
                const bool horizontal = std::abs(pts[i - 1].y() - pts[i].y()) < 0.5;
                if (horizontal) pts[i + 1].setY(pts[i].y()); else pts[i + 1].setX(pts[i].x());
                if (horizontal) pts[i + 2].setY(pts[i].y()); else pts[i + 2].setX(pts[i].x());
            }
        }
        pts = simplify(pts);
        if (pts != w.points && pts.size() >= 2) {
            before.wires << w;
            Wire n = w;
            n.points = pts;
            after.wires << n;
        }
    }
    if (!after.isEmpty())
        m_undo->push(new ModifyItemsCommand(m_circuit, before, after, tr("Straighten wires")));
}

void SchematicScene::setDesignations(const QString& prefix, int first)
{
    if (simulating())
        return;
    QVector<Component> comps = selection().components;
    if (comps.isEmpty())
        for (const Component& c : m_circuit->components()) comps << c;
    std::sort(comps.begin(), comps.end(), [](const Component& a, const Component& b) {
        return qFuzzyCompare(a.pos.y(), b.pos.y()) ? a.pos.x() < b.pos.x() : a.pos.y() < b.pos.y();
    });
    QHash<QString, int> next;
    Selection before, after;
    for (const Component& c : std::as_const(comps)) {
        const SymbolDef* s = m_library.find(c.symbolId);
        if (!s || s->netLabel || s->hidden || s->prefix.isEmpty()) continue;
        if (!prefix.isEmpty() && s->prefix != prefix) continue;
        const int n = next.value(s->prefix, first);
        next[s->prefix] = n + 1;
        const QString ref = s->prefix + QString::number(n);
        if (ref == c.designator) continue;
        before.components << c;
        Component m = c;
        m.designator = ref;
        after.components << m;
    }
    if (!after.isEmpty())
        m_undo->push(new ModifyItemsCommand(m_circuit, before, after, tr("Set designations")));
}

void SchematicScene::setPropDelays(int ticks)
{
    Selection before, after;
    for (const Component& c : selection().components) {
        const SymbolDef* s = m_library.find(c.symbolId);
        const QString kind = s ? s->model.value(u"kind").toString() : QString();
        if (kind != u"gate" && kind != u"dff" && kind != u"jkff" && kind != u"tribuf") continue;
        before.components << c;
        Component n = c;
        n.props.insert(QStringLiteral("delay"), std::clamp(ticks, 1, 14));
        after.components << n;
    }
    if (!after.isEmpty())
        m_undo->push(new ModifyItemsCommand(m_circuit, before, after, tr("Set propagation delays")));
}

void SchematicScene::duplicateSelection()
{
    if (restricted(QStringLiteral("edit")))
        return;
    if (simulating() || selection().isEmpty())
        return;
    copySelection();
    paste();
}

int SchematicScene::netAt(QPointF p) const
{
    for (const WireItem* w : m_wireItems)
        if (!w->wire().bus && WireGeometry::onPolyline(p, w->wire().points))
            return m_netlist.wireNet.value(w->id(), -1);
    for (const ComponentItem* item : m_componentItems)
        for (int i = 0; i < item->symbol()->pins.size(); ++i)
            if (near(m_library.pinScenePos(item->component(), i), p, 4))
                return m_netlist.netOf(item->id(), i);
    return -1;
}

void SchematicScene::toggleProbe(QPointF p)
{
    QVector<QPointF> probes = m_circuit->probes();
    for (int i = 0; i < probes.size(); ++i) {
        if (near(probes[i], p, 8)) {
            probes.remove(i);
            m_undo->push(new SetProbesCommand(m_circuit, probes, tr("Remove probe")));
            return;
        }
    }
    QPointF pin;
    if (pinAt(p, &pin))
        p = pin;
    else if (!onConductor(p)) {
        emit statusMessage(tr("Place probes on a wire or a pin"));
        return;
    }
    probes << p;
    m_undo->push(new SetProbesCommand(m_circuit, probes, tr("Add probe")));
}

QVector<int> SchematicScene::probeNets() const
{
    QVector<int> out;
    for (QPointF p : m_circuit->probes())
        out << netAt(p);
    return out;
}

void SchematicScene::setAnalogOverlay(AnalogOverlay overlay)
{
    m_overlay = std::move(overlay);
    update();
}

void SchematicScene::clearAnalogOverlay()
{
    if (!m_overlay)
        return;
    m_overlay.reset();
    update();
}

// ---------------------------------------------------------------- dragging

void SchematicScene::beginDrag()
{
    m_drag = {};
    for (QGraphicsItem* it : selectedItems()) {
        if (auto* c = qgraphicsitem_cast<ComponentItem*>(it))
            m_drag.componentStart.insert(c->id(), c->pos());
        else if (auto* t = qgraphicsitem_cast<TextNoteItem*>(it))
            m_drag.textStart.insert(t->id(), t->pos());
        else if (auto* w = qgraphicsitem_cast<WireItem*>(it))
            m_drag.wires << w->id();
        else if (it == m_titleItem)
            m_drag.titleStart = it->pos();
    }
    m_drag.active = !m_drag.componentStart.isEmpty() || !m_drag.textStart.isEmpty() || !m_drag.wires.isEmpty()
                 || m_drag.titleStart;
}

QHash<ItemId, QVector<QPointF>> SchematicScene::rubberBand() const
{
    struct Anchor { QPointF at; QPointF delta; };
    QVector<Anchor> anchors;
    for (auto it = m_drag.componentStart.cbegin(); it != m_drag.componentStart.cend(); ++it) {
        const ComponentItem* item = m_componentItems.value(it.key());
        if (!item)
            continue;
        const QPointF delta = item->pos() - it.value();
        if (delta.isNull())
            continue;
        const Component* c = m_circuit->component(it.key());
        for (int i = 0; i < item->symbol()->pins.size(); ++i)
            anchors << Anchor{m_library.pinScenePos(*c, i), delta};
    }
    QHash<ItemId, QVector<QPointF>> out;
    if (anchors.isEmpty())
        return out;
    for (const WireItem* w : m_wireItems) {
        if (m_drag.wires.contains(w->id()))
            continue;
        QVector<QPointF> pts = w->wire().points;
        const QPointF first = pts.first(), last = pts.last();
        bool changed = false;
        for (const Anchor& a : anchors) {
            if (near(first, a.at)) { moveEnd(pts, 0, a.delta); changed = true; break; }
        }
        for (const Anchor& a : anchors) {
            if (near(last, a.at)) { moveEnd(pts, int(pts.size()) - 1, a.delta); changed = true; break; }
        }
        if (changed)
            out.insert(w->id(), simplify(pts));
    }
    return out;
}

void SchematicScene::updateDragPreview()
{
    const auto rb = rubberBand();
    for (WireItem* w : std::as_const(m_wireItems)) {
        if (m_drag.wires.contains(w->id()))
            continue;
        if (auto it = rb.constFind(w->id()); it != rb.cend())
            w->setPreview(*it);
        else
            w->clearPreview();
    }
}

void SchematicScene::commitDrag()
{
    m_drag.active = false;
    Selection before, after;
    for (auto it = m_drag.componentStart.cbegin(); it != m_drag.componentStart.cend(); ++it) {
        const ComponentItem* item = m_componentItems.value(it.key());
        if (!item || item->pos() == it.value())
            continue;
        Component c = *m_circuit->component(it.key());
        before.components << c;
        c.pos = item->pos();
        after.components << c;
    }
    for (auto it = m_drag.textStart.cbegin(); it != m_drag.textStart.cend(); ++it) {
        const TextNoteItem* item = m_textItems.value(it.key());
        if (!item || item->pos() == it.value())
            continue;
        TextNote t = *m_circuit->text(it.key());
        before.texts << t;
        t.pos = item->pos();
        after.texts << t;
    }
    for (ItemId id : std::as_const(m_drag.wires)) {
        const WireItem* item = m_wireItems.value(id);
        if (!item || item->pos().isNull())
            continue;
        Wire w = *m_circuit->wire(id);
        before.wires << w;
        w.points = item->movedPoints();
        after.wires << w;
    }
    const auto rb = rubberBand();
    for (auto it = rb.cbegin(); it != rb.cend(); ++it) {
        Wire w = *m_circuit->wire(it.key());
        before.wires << w;
        w.points = it.value();
        after.wires << w;
    }
    for (WireItem* w : std::as_const(m_wireItems))
        w->clearPreview();
    const bool titleMoved = m_titleItem && m_drag.titleStart && m_titleItem->pos() != *m_drag.titleStart;
    if (after.isEmpty() && !titleMoved)
        return;
    m_undo->beginMacro(tr("Move"));
    if (!after.isEmpty())
        m_undo->push(new ModifyItemsCommand(m_circuit, before, after, {}));
    if (titleMoved) {
        TitleBlock tb = m_circuit->titleBlock();
        tb.pos = m_titleItem->pos();
        m_undo->push(new SetTitleBlockCommand(m_circuit, tb, {}));
    }
    m_undo->endMacro();
}

// ---------------------------------------------------------------- editing

Selection SchematicScene::selection() const
{
    Selection s;
    for (QGraphicsItem* it : selectedItems()) {
        if (auto* c = qgraphicsitem_cast<ComponentItem*>(it))
            s.components << *m_circuit->component(c->id());
        else if (auto* w = qgraphicsitem_cast<WireItem*>(it))
            s.wires << *m_circuit->wire(w->id());
        else if (auto* t = qgraphicsitem_cast<TextNoteItem*>(it))
            s.texts << *m_circuit->text(t->id());
    }
    return s;
}

void SchematicScene::rotateSelection()
{
    if (m_ghost) {
        m_ghostComponent.rotation = (m_ghostComponent.rotation + 1) % 4;
        m_ghost->sync(m_ghostComponent);
        return;
    }
    transformSelection(true);
}

void SchematicScene::mirrorSelection()
{
    if (m_ghost) {
        m_ghostComponent.mirrored = !m_ghostComponent.mirrored;
        m_ghost->sync(m_ghostComponent);
        return;
    }
    transformSelection(false);
}

void SchematicScene::transformSelection(bool rotate)
{
    if (restricted(QStringLiteral("edit")))
        return;
    if (simulating())
        return;
    const Selection before = selection();
    if (before.isEmpty())
        return;
    QRectF bounds;
    for (QGraphicsItem* it : selectedItems())
        bounds = bounds.united(it->sceneBoundingRect());
    const QPointF c = snap(bounds.center());
    auto map = [&](QPointF p) {
        const QPointF d = p - c;
        return rotate ? c + QPointF(-d.y(), d.x()) : QPointF(2 * c.x() - p.x(), p.y());
    };

    Selection after = before;
    for (Component& x : after.components) {
        x.pos = snap(map(x.pos));
        if (rotate) {
            x.rotation = (x.rotation + 1) % 4;
        } else {
            x.rotation = (4 - x.rotation) % 4;
            x.mirrored = !x.mirrored;
        }
    }
    for (Wire& w : after.wires)
        for (QPointF& p : w.points)
            p = snap(map(p));
    for (TextNote& t : after.texts)
        t.pos = snap(map(t.pos));
    m_undo->push(new ModifyItemsCommand(m_circuit, before, after, rotate ? tr("Rotate") : tr("Mirror")));
}

void SchematicScene::deleteSelection()
{
    if (restricted(QStringLiteral("delete")))
        return;
    if (simulating())
        return;
    const Selection sel = selection();
    if (!sel.isEmpty())
        m_undo->push(new RemoveItemsCommand(m_circuit, sel, tr("Delete")));
}

void SchematicScene::selectAll()
{
    for (QGraphicsItem* it : items())
        if (it->flags() & QGraphicsItem::ItemIsSelectable)
            it->setSelected(true);
}

void SchematicScene::copySelection()
{
    if (restricted(QStringLiteral("edit")))
        return;
    const Selection sel = selection();
    if (sel.isEmpty())
        return;
    Circuit temp;
    for (Component c : sel.components) { c.id = 0; temp.addComponent(c); }
    for (Wire w : sel.wires) { w.id = 0; temp.addWire(w); }
    for (TextNote t : sel.texts) { t.id = 0; temp.addText(t); }
    auto* mime = new QMimeData;
    mime->setData(kMime, NativeFormat::serialize(temp));
    QApplication::clipboard()->setMimeData(mime);
}

void SchematicScene::cutSelection()
{
    if (restricted(QStringLiteral("edit")))
        return;
    copySelection();
    const Selection sel = selection();
    if (!sel.isEmpty())
        m_undo->push(new RemoveItemsCommand(m_circuit, sel, tr("Cut")));
}

void SchematicScene::paste()
{
    if (restricted(QStringLiteral("edit")))
        return;
    if (simulating())
        return;
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(kMime))
        return;
    Circuit temp;
    if (NativeFormat::deserialize(mime->data(kMime), temp, nullptr))
        insertCircuit(temp, QPointF(20, 20), tr("Paste"));
}

void SchematicScene::insertCircuit(const Circuit& source, QPointF offset, const QString& undoText)
{
    if (restricted(QStringLiteral("edit")))
        return;
    if (simulating())
        return;
    Selection sel = fromCircuit(source);
    if (sel.isEmpty())
        return;
    // designators already in use get the next free number
    Circuit names;
    for (const Component& c : m_circuit->components())
        names.addComponent(c);
    for (Component& c : sel.components) {
        c.id = 0;
        c.pos += offset;
        bool clash = false;
        for (const Component& e : names.components())
            clash |= e.designator == c.designator;
        if (clash) {
            const SymbolDef* s = m_library.find(c.symbolId);
            c.designator = names.nextDesignator(s ? s->prefix : QStringLiteral("U"));
        }
        names.addComponent(c);
        c.id = 0;
    }
    for (Wire& w : sel.wires) {
        w.id = 0;
        for (QPointF& p : w.points) p += offset;
    }
    for (TextNote& t : sel.texts) {
        t.id = 0;
        t.pos += offset;
    }
    auto* cmd = new AddItemsCommand(m_circuit, sel, undoText);
    m_undo->push(cmd);
    clearSelection();
    for (const Component& c : cmd->items().components)
        if (auto* it = m_componentItems.value(c.id)) it->setSelected(true);
    for (const Wire& w : cmd->items().wires)
        if (auto* it = m_wireItems.value(w.id)) it->setSelected(true);
    for (const TextNote& t : cmd->items().texts)
        if (auto* it = m_textItems.value(t.id)) it->setSelected(true);
}

// Merge: the other circuit goes below this one, left edges aligned.
void SchematicScene::mergeCircuit(const Circuit& source)
{
    QRectF incoming;
    for (const Component& c : source.components()) {
        const SymbolDef* s = m_library.find(c.symbolId);
        incoming |= QRectF(c.pos, QSizeF(1, 1)) | (s ? s->bounds.translated(c.pos) : QRectF());
    }
    for (const Wire& w : source.wires())
        for (QPointF p : w.points) incoming |= QRectF(p, QSizeF(1, 1));
    for (const TextNote& t : source.texts()) incoming |= QRectF(t.pos, QSizeF(1, 1));
    QPointF offset;
    if (!m_circuit->components().isEmpty() || !m_circuit->wires().isEmpty() || !m_circuit->texts().isEmpty()) {
        const QRectF here = itemsRect();
        offset = snap(QPointF(here.left() - incoming.left(), here.bottom() + 100 - incoming.top()));
    }
    insertCircuit(source, offset, tr("Merge"));
}

void SchematicScene::editItem(QGraphicsItem* item)
{
    if (simulating() || !item)
        return;
    if (item == m_titleItem) {
        editTitleBlock();
        return;
    }
    if (auto* c = qgraphicsitem_cast<ComponentItem*>(item)) {
        if (restricted(QStringLiteral("properties")))
            return;
        const Component* current = m_circuit->component(c->id());
        if (!current)
            return;
        const Component before = *current;
        Component after;
        if (c->symbol()->model.value(u"kind").toString() == u"dataseq") {
            DataSequencerDialog dlg(before, *c->symbol(), dialogParent());
            if (dlg.exec() != QDialog::Accepted)
                return;
            after = dlg.result();
        } else {
            PropertiesDialog dlg(before, *c->symbol(), dialogParent());
            if (dlg.exec() != QDialog::Accepted)
                return;
            after = dlg.result();
        }
        if (after.designator == before.designator && after.label == before.label && after.props == before.props)
            return;
        Selection b, a;
        b.components << before;
        a.components << after;
        m_undo->push(new ModifyItemsCommand(m_circuit, b, a, tr("Edit %1").arg(before.designator)));
    } else if (auto* t = qgraphicsitem_cast<TextNoteItem*>(item)) {
        const TextNote before = *m_circuit->text(t->id());
        bool ok = false;
        const QString text = QInputDialog::getMultiLineText(dialogParent(), tr("Edit text"), tr("Text:"), before.text, &ok);
        if (!ok || text == before.text)
            return;
        TextNote after = before;
        after.text = text;
        Selection b, a;
        b.texts << before;
        a.texts << after;
        m_undo->push(new ModifyItemsCommand(m_circuit, b, a, tr("Edit text")));
    }
}

bool SchematicScene::isMemory(const SymbolDef& symbol)
{
    const QString kind = symbol.model.value(u"kind").toString();
    return kind == u"ram" || kind == u"rom";
}

void SchematicScene::editMemory(ItemId id)
{
    const Component* current = m_circuit->component(id);
    const ComponentItem* item = m_componentItems.value(id);
    if (!current || !item || !isMemory(*item->symbol()))
        return;
    const QString title = tr("Edit PROM/RAM %1").arg(current->designator);
    if (m_sim) {
        const QVector<int> words = m_sim->memory(id);
        if (words.isEmpty())
            return;
        MemoryEditorDialog dlg(title, words, dialogParent());
        if (dlg.exec() == QDialog::Accepted && dlg.words() != words) {
            m_sim->setMemory(id, dlg.words());
            update();
        }
        return;
    }
    int addressBits = 0;
    static const QRegularExpression address(QStringLiteral("^A\\d+$"));
    for (const SymbolPin& p : item->symbol()->pins)
        if (address.match(p.name).hasMatch()) ++addressBits;
    const QString before = current->props.value(QStringLiteral("data")).toString();
    MemoryEditorDialog dlg(title, MemoryEditorDialog::fromText(before, 1 << addressBits), dialogParent());
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString text = MemoryEditorDialog::toText(dlg.words());
    if (text == MemoryEditorDialog::toText(MemoryEditorDialog::fromText(before, 1 << addressBits)))
        return;
    Component after = *current;
    after.props.insert(QStringLiteral("data"), text);
    Selection b, a;
    b.components << *current;
    a.components << after;
    m_undo->push(new ModifyItemsCommand(m_circuit, b, a, tr("Edit %1 contents").arg(current->designator)));
}

int SchematicScene::findAndSelect(const QString& text)
{
    clearSelection();
    const QString needle = text.trimmed();
    if (needle.isEmpty())
        return 0;
    int count = 0;
    for (ComponentItem* item : std::as_const(m_componentItems)) {
        const Component& c = item->component();
        if (c.designator.contains(needle, Qt::CaseInsensitive) || c.label.contains(needle, Qt::CaseInsensitive)
            || item->symbol()->name.contains(needle, Qt::CaseInsensitive)) {
            item->setSelected(true);
            ++count;
        }
    }
    return count;
}

void SchematicScene::editTitleBlock()
{
    if (simulating())
        return;
    TitleBlockDialog dlg(m_circuit->titleBlock(), dialogParent());
    if (dlg.exec() != QDialog::Accepted)
        return;
    TitleBlock tb = dlg.result();
    if (!tb.visible) {
        tb.visible = true;
        if (tb.pos.isNull())
            tb.pos = snap(itemsRect().bottomRight() - QPointF(TitleBlockItem::kWidth, -20));
    }
    m_undo->push(new SetTitleBlockCommand(m_circuit, tb, tr("Edit title block")));
}

void SchematicScene::setTitleBlockVisible(bool visible)
{
    if (simulating() || m_circuit->titleBlock().visible == visible)
        return;
    TitleBlock tb = m_circuit->titleBlock();
    tb.visible = visible;
    if (visible && tb.pos.isNull())
        tb.pos = snap(itemsRect().bottomRight() - QPointF(TitleBlockItem::kWidth, -20));
    m_undo->push(new SetTitleBlockCommand(m_circuit, tb, visible ? tr("Show title block") : tr("Hide title block")));
}

// ---------------------------------------------------------------- simulation

void SchematicScene::run()
{
    if (!m_sim) {
        if (m_tool != Tool::Probe)
            setTool(Tool::Select);      // the probe tool reads nodes while simulating
        clearSelection();
        rebuildNetlist();
        m_faulted.reset();
        if (faults::anyActive(simCircuit())) {
            m_faulted = faults::copy(simCircuit());
            faults::apply(*m_faulted, m_netlist, m_library, false);
        }
        m_sim = std::make_unique<sim::EventSimulator>();
        m_sim->build(simCircuit(), m_library, m_netlist);
        m_sim->reset();
        m_lanes.clear();
        m_breaks.clear();
        for (const ComponentItem* item : std::as_const(m_componentItems)) {
            const Component& c = item->component();
            if (c.symbolId != u"scope") continue;
            const int net = m_netlist.netOf(c.id, 0);
            m_lanes << Lane{c.label.isEmpty() ? (c.designator.isEmpty() ? m_netlist.nets.value(net).name : c.designator) : c.label,
                            net, c.id};
            m_breaks.insert(c.id, std::clamp(c.props.value(QStringLiteral("break")).toInt(), 0, 2));
        }
        m_breakPrev.clear();
        m_breakMet = false;
        const QVector<int> probes = probeNets();
        for (int i = 0; i < probes.size(); ++i)
            if (probes[i] >= 0)
                m_lanes << Lane{QStringLiteral("%1: %2").arg(i + 1).arg(m_netlist.nets[probes[i]].name), probes[i]};
        QVector<int> nets;
        for (const Lane& l : std::as_const(m_lanes)) nets << l.net;
        m_sim->watch(nets);
        const QStringList problems = m_flatWarnings + m_sim->warnings();
        if (!problems.isEmpty())
            emit statusMessage(tr("Simulation: %n problem(s); first: %1", nullptr, int(problems.size()))
                                   .arg(problems.first()));
    }
    m_timer.start();
    update();
    emit simulationChanged();
}

void SchematicScene::pause()
{
    m_timer.stop();
    emit simulationChanged();
}

void SchematicScene::stepSimulation()
{
    if (!m_sim) {
        run();
        m_timer.stop();
    }
    m_sim->stepToNextEvent();
    emit simTimeChanged(m_sim->time());
    update();
    emit simulationChanged();
}

void SchematicScene::resetSimulation()
{
    if (!m_sim)
        return;
    m_sim->reset();
    emit simTimeChanged(0);
    update();
}

void SchematicScene::stopSimulation()
{
    if (!m_sim)
        return;
    simInteractRelease();
    m_timer.stop();
    persistSimProps();
    m_sim.reset();
    if (m_faulted)
        rebuildNetlist();       // without the faults' rewiring
    update();
    emit simulationChanged();
}

void SchematicScene::persistSimProps()
{
    Selection before, after;
    for (const Component& c : m_circuit->components()) {
        QVariantMap p = m_sim->persistentProps(c.id);
        if (const int b = m_breaks.value(c.id, -1); b >= 0 && b != c.props.value(QStringLiteral("break")).toInt())
            p.insert(QStringLiteral("break"), b);
        if (p.isEmpty())
            continue;
        Component n = c;
        for (auto it = p.cbegin(); it != p.cend(); ++it)
            n.props.insert(it.key(), it.value());
        if (n.props != c.props) {
            before.components << c;
            after.components << n;
        }
    }
    if (!after.isEmpty())
        m_undo->push(new ModifyItemsCommand(m_circuit, before, after, tr("Switch positions")));
}

void SchematicScene::advance()
{
    bool breaks = false;
    for (int b : std::as_const(m_breaks)) breaks |= b != 0;
    if (!breaks) {
        m_sim->step(m_ticksPerFrame);
    } else {
        // one tick at a time so the simulation stops exactly at the break
        for (int i = 0; i < m_ticksPerFrame; ++i) {
            m_sim->step(1);
            if (breakpointHit()) {
                pause();
                emit statusMessage(tr("Breakpoint at tick %1").arg(m_sim->time()));
                break;
            }
        }
    }
    emit simTimeChanged(m_sim->time());
    update();
}

int SchematicScene::breakCondition(ItemId scope) const
{
    if (m_sim)
        return m_breaks.value(scope);
    const Component* c = m_circuit->component(scope);
    return c ? std::clamp(c->props.value(QStringLiteral("break")).toInt(), 0, 2) : 0;
}

void SchematicScene::setBreakCondition(ItemId scope, int condition)
{
    if (m_sim) {
        if (m_breaks.contains(scope)) m_breaks[scope] = std::clamp(condition, 0, 2);
        m_breakPrev.clear();
        m_breakMet = false;
        return;
    }
    const Component* c = m_circuit->component(scope);
    if (!c || breakCondition(scope) == condition)
        return;
    Component n = *c;
    n.props.insert(QStringLiteral("break"), std::clamp(condition, 0, 2));
    Selection b, a;
    b.components << *c;
    a.components << n;
    m_undo->push(new ModifyItemsCommand(m_circuit, b, a, tr("Set breakpoint")));
}

// CircuitMaker's breakpoint types: Level-And/Or stop when the SCOPE levels
// (all or any) start matching; Edge-And/Or when they change to the condition
// in this tick (all together or any one).
bool SchematicScene::breakpointHit()
{
    const AnalysisSettings& a = m_circuit->analysis();
    bool used = false, any = false, all = true;
    for (const Lane& l : std::as_const(m_lanes)) {
        const int b = l.component ? m_breaks.value(l.component) : 0;
        if (!b || l.net < 0) continue;
        used = true;
        const sim::Logic target = b == 2 ? sim::Logic::L1 : sim::Logic::L0;
        const sim::Logic v = m_sim->netValue(l.net);
        const sim::Logic prev = m_breakPrev.value(l.net, v);
        m_breakPrev[l.net] = v;
        const bool met = v == target && (!a.breakEdge || prev != target);
        any |= met;
        all &= met;
    }
    const bool met = used && (a.breakAll ? all : any);
    if (a.breakEdge)
        return met;
    const bool started = met && !m_breakMet;
    m_breakMet = met;
    return started;
}

quint64 SchematicScene::simTime() const { return m_sim ? m_sim->time() : 0; }

int SchematicScene::simState(ItemId id) const { return m_sim ? m_sim->componentState(id) : 0; }

QColor SchematicScene::wireColor(ItemId id) const
{
    if (!m_sim || m_exporting || !m_trace)
        return QColor(0x00, 0x30, 0xff);
    switch (m_sim->netValue(m_netlist.wireNet.value(id, -1))) {
    case sim::Logic::L1: return {0xe0, 0x10, 0x10};
    case sim::Logic::L0: return {0x10, 0x50, 0xc0};
    case sim::Logic::X: return {0xf0, 0x90, 0x00};
    case sim::Logic::Z: return {0x90, 0x90, 0x90};
    }
    return kInk;
}

void SchematicScene::simInteractPress(QGraphicsSceneMouseEvent* e)
{
    ComponentItem* hit = nullptr;
    for (QGraphicsItem* it : items(e->scenePos()))
        if ((hit = qgraphicsitem_cast<ComponentItem*>(it)) && hit->id() != 0)
            break;
    if (!hit || !m_sim->hasModel(hit->id()))
        return;
    const QString& d = hit->symbol()->display;
    if (d == u"logicswitch" || d == u"spst") {
        m_sim->interact(hit->id(), QStringLiteral("toggle"));
    } else if (d == u"button") {
        m_sim->interact(hit->id(), QStringLiteral("press"));
        m_heldButton = hit->id();
    } else if (d == u"hexkey") {
        const int key = hit->hexKeyAt(hit->mapFromScene(e->scenePos()));
        if (key < 0)
            return;
        m_sim->interact(hit->id(), QStringLiteral("key"), key);
        m_heldButton = hit->id();
    } else if (d == u"asciikey") {
        bool ok = false;
        const QString text = QInputDialog::getText(dialogParent(), tr("ASCII key"), tr("Character:"), QLineEdit::Normal, {}, &ok);
        if (ok && !text.isEmpty()) {
            m_sim->interact(hit->id(), QStringLiteral("key"), text.at(0).unicode() & 0x7f);
            m_sim->interact(hit->id(), QStringLiteral("release"));
        }
    }
    update();
}

void SchematicScene::simInteractRelease()
{
    if (m_heldButton && m_sim)
        m_sim->interact(m_heldButton, QStringLiteral("release"));
    m_heldButton = 0;
    update();
}

// ---------------------------------------------------------------- events

void SchematicScene::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    const QPointF p = snap(e->scenePos());
    if (simulating()) {
        if (e->button() == Qt::LeftButton && m_tool != Tool::Probe)
            simInteractPress(e);
        return;
    }
    if (e->button() == Qt::RightButton) {
        m_suppressMenu = !m_wirePoints.isEmpty() || m_tool != Tool::Select;
        if (!m_wirePoints.isEmpty())
            m_wirePoints.size() >= 2 ? finishWire() : cancelWire();
        else if (m_tool != Tool::Select)
            setTool(Tool::Select);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return QGraphicsScene::mousePressEvent(e);

    switch (m_tool) {
    case Tool::Place:
        if (m_ghost) {
            const bool keep = e->modifiers() & Qt::ShiftModifier;
            placeGhost(e->scenePos());
            if (!keep)
                setTool(Tool::Select);
        }
        return;
    case Tool::Wire:
    case Tool::Bus:
        if (m_wirePoints.isEmpty())
            m_drawingBus = m_tool == Tool::Bus;
        wireClick(p);
        return;
    case Tool::NetLabel:
        placeNetLabel(p);
        return;
    case Tool::Probe:
        toggleProbe(p);
        return;
    case Tool::Delete:
        deleteAt(e->scenePos(), e->modifiers() & Qt::ShiftModifier);
        return;
    case Tool::Zoom:
        emit zoomRequested(e->scenePos(), e->modifiers() & Qt::ShiftModifier ? 0.5 : 2.0);
        return;
    case Tool::Text: {
        bool ok = false;
        const QString text = QInputDialog::getMultiLineText(dialogParent(), tr("Add text"), tr("Text:"), {}, &ok);
        if (ok && !text.trimmed().isEmpty()) {
            Selection sel;
            sel.texts << TextNote{0, p, text, 10};
            m_undo->push(new AddItemsCommand(m_circuit, sel, tr("Add text")));
        }
        setTool(Tool::Select);
        return;
    }
    case Tool::Select:
        break;
    }

    if (!m_wirePoints.isEmpty()) {
        wireClick(p);
        return;
    }
    // pressing on a pin end starts a wire, as in CircuitMaker
    QPointF pin;
    if (!(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) && !restricted(QStringLiteral("wire"))
        && pinAt(e->scenePos(), &pin)) {
        clearSelection();
        m_wireFromSelect = true;
        m_wirePoints = {pin};
        update();
        return;
    }
    // Alt+click on a wire highlights its whole node (CircuitMaker)
    const int previousHighlight = m_highlightNet;
    m_highlightNet = -1;
    if (e->modifiers() & Qt::AltModifier) {
        const int net = netAt(snap(e->scenePos())) >= 0 ? netAt(snap(e->scenePos())) : netAt(e->scenePos());
        if (net >= 0) {
            m_highlightNet = net;
            clearSelection();
            for (WireItem* w : std::as_const(m_wireItems))
                if (m_netlist.wireNet.value(w->id(), -1) == net)
                    w->setSelected(true);
            update();
            return;
        }
    }
    if (previousHighlight >= 0)
        update();
    if (!restricted(QStringLiteral("wire")) && !restricted(QStringLiteral("edit")) && beginWireEdit(e))
        return;
    QGraphicsScene::mousePressEvent(e);
    if (!restricted(QStringLiteral("edit")))
        beginDrag();
}

// A press on a lone wire edits its shape instead of moving it: near a free
// end the end follows the mouse, elsewhere the segment slides sideways.
bool SchematicScene::beginWireEdit(QGraphicsSceneMouseEvent* e)
{
    if (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))
        return false;
    WireItem* hit = nullptr;
    for (QGraphicsItem* it : items(e->scenePos()))
        if ((hit = qgraphicsitem_cast<WireItem*>(it)))
            break;
    if (!hit)
        return false;
    // part of a larger selection: move everything together
    if (hit->isSelected() && selectedItems().size() > 1)
        return false;

    const QVector<QPointF>& pts = hit->wire().points;
    const QPointF at = e->scenePos();
    WireEdit edit;
    edit.wire = hit->id();
    edit.original = pts;
    edit.start = snap(at);
    if (near(at, pts.first(), 5) && !pinAt(pts.first(), nullptr)) {
        edit.endpoint = 0;
    } else if (near(at, pts.last(), 5) && !pinAt(pts.last(), nullptr)) {
        edit.endpoint = int(pts.size()) - 1;
    } else {
        edit.segment = segmentAt(pts, at);
        if (edit.segment < 0)
            return false;
    }
    clearSelection();
    hit->setSelected(true);
    m_wireEdit = edit;
    return true;
}

int SchematicScene::segmentAt(const QVector<QPointF>& pts, QPointF p)
{
    int best = -1;
    double bestDist = 6;
    for (int i = 0; i + 1 < pts.size(); ++i) {
        const QLineF seg(pts[i], pts[i + 1]);
        const QPointF ab = pts[i + 1] - pts[i];
        const double len2 = QPointF::dotProduct(ab, ab);
        const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - pts[i], ab) / len2, 0.0, 1.0) : 0;
        const double d = QLineF(p, pts[i] + t * ab).length();
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

QVector<QPointF> SchematicScene::editedWire(QPointF cursor) const
{
    QVector<QPointF> pts = m_wireEdit->original;
    const QPointF delta = cursor - m_wireEdit->start;
    if (m_wireEdit->endpoint >= 0) {
        moveEnd(pts, m_wireEdit->endpoint, delta);
        return pts;
    }
    int i = m_wireEdit->segment;
    const bool horizontal = std::abs(pts[i].y() - pts[i + 1].y()) < 0.5;
    const bool vertical = std::abs(pts[i].x() - pts[i + 1].x()) < 0.5;
    const QPointF d = horizontal ? QPointF(0, delta.y()) : vertical ? QPointF(delta.x(), 0) : delta;
    // keep the wire ends where they are: an end segment grows an elbow
    if (i == 0) {
        pts.prepend(pts.first());
        ++i;
    }
    if (i + 1 == pts.size() - 1)
        pts.append(pts.last());
    pts[i] += d;
    pts[i + 1] += d;
    return pts;
}

void SchematicScene::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    m_cursor = snap(e->scenePos());
    emit cursorMoved(m_cursor);
    if (simulating()) {
        if (m_tool == Tool::Probe) {
            const int net = netAt(e->scenePos());
            if (net != m_probeNet || net >= 0) {
                m_probeNet = net;
                update();
            }
        }
        return;
    }
    if (m_ghost) {
        m_ghostComponent.pos = m_cursor;
        m_ghost->setPos(m_cursor);
        return;
    }
    if (!m_wirePoints.isEmpty()) {
        update();
        return;
    }
    if (m_wireEdit) {
        if (WireItem* w = m_wireItems.value(m_wireEdit->wire))
            w->setPreview(editedWire(m_cursor));
        return;
    }
    if (restricted(QStringLiteral("edit")) && (e->buttons() & Qt::LeftButton))
        return;     // the circuit is locked against moving
    QGraphicsScene::mouseMoveEvent(e);
    if (m_drag.active && (e->buttons() & Qt::LeftButton))
        updateDragPreview();
    else if (e->buttons() == Qt::NoButton)
        updateHoverCursor(e->scenePos());
}

void SchematicScene::updateHoverCursor(QPointF p)
{
    if (m_tool != Tool::Select || views().isEmpty())
        return;
    Qt::CursorShape shape = Qt::ArrowCursor;
    if (pinAt(p, nullptr)) {
        shape = Qt::CrossCursor;
    } else {
        for (QGraphicsItem* it : items(p)) {
            auto* w = qgraphicsitem_cast<WireItem*>(it);
            if (!w)
                continue;
            const QVector<QPointF>& pts = w->wire().points;
            const int seg = segmentAt(pts, p);
            if (seg >= 0)
                shape = std::abs(pts[seg].y() - pts[seg + 1].y()) < 0.5 ? Qt::SizeVerCursor : Qt::SizeHorCursor;
            break;
        }
    }
    for (QGraphicsView* v : views())
        if (v->viewport()->cursor().shape() != shape)
            v->viewport()->setCursor(shape);
}

void SchematicScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
{
    if (simulating()) {
        simInteractRelease();
        return;
    }
    if (m_wireFromSelect && m_wirePoints.size() == 1 && e->button() == Qt::LeftButton) {
        // press-drag-release from a pin draws the wire in one gesture
        const QPointF p = snap(e->scenePos());
        if (!near(p, m_wirePoints.first()))
            wireClick(p);
        return;
    }
    if (m_wireEdit) {
        const WireEdit edit = *m_wireEdit;
        const QVector<QPointF> pts = simplify(editedWire(snap(e->scenePos())));
        m_wireEdit.reset();
        WireItem* item = m_wireItems.value(edit.wire);
        if (!item)
            return;
        item->clearPreview();
        if (pts != edit.original && pts.size() >= 2) {
            Selection before, after;
            before.wires << *m_circuit->wire(edit.wire);
            Wire w = before.wires.first();
            w.points = pts;
            after.wires << w;
            m_undo->push(new ModifyItemsCommand(m_circuit, before, after, tr("Reshape wire")));
        }
        return;
    }
    QGraphicsScene::mouseReleaseEvent(e);
    if (m_drag.active)
        commitDrag();
}

void SchematicScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e)
{
    if (simulating())
        return;
    if (!m_wirePoints.isEmpty()) {
        finishWire();
        return;
    }
    if (m_tool != Tool::Select)
        return;
    for (QGraphicsItem* it : items(e->scenePos())) {
        if (it->type() == ComponentItem::Type || it->type() == TextNoteItem::Type || it->type() == TitleBlockItem::Type) {
            if (it == m_ghost)
                continue;
            editItem(it);
            return;
        }
    }
}

void SchematicScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* e)
{
    if (std::exchange(m_suppressMenu, false) || m_ghost || !m_wirePoints.isEmpty() || m_tool != Tool::Select)
        return;
    QGraphicsItem* hit = nullptr;
    for (QGraphicsItem* it : items(e->scenePos())) {
        if (it == m_ghost)
            continue;
        if (it->type() == ComponentItem::Type || it->type() == WireItem::Type || it->type() == TextNoteItem::Type
            || it->type() == TitleBlockItem::Type) {
            hit = it;
            break;
        }
    }
    if (!simulating()) {
        if (!hit || !hit->isSelected())
            clearSelection();
        if (hit)
            hit->setSelected(true);
    }
    e->accept();
    emit contextMenuRequested(hit, e->screenPos());
}

void SchematicScene::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        if (!m_wirePoints.isEmpty())
            cancelWire();
        else if (m_tool != Tool::Select)
            setTool(Tool::Select);
        else
            clearSelection();
        return;
    }
    if (!simulating() && !m_ghost && m_wirePoints.isEmpty() && e->modifiers() == Qt::NoModifier) {
        const QPointF d = e->key() == Qt::Key_Left ? QPointF(-kGrid, 0) : e->key() == Qt::Key_Right ? QPointF(kGrid, 0)
                        : e->key() == Qt::Key_Up ? QPointF(0, -kGrid) : e->key() == Qt::Key_Down ? QPointF(0, kGrid) : QPointF();
        if (!d.isNull() && !selection().components.isEmpty()) {
            nudgeSelection(d);
            return;
        }
    }
    if (m_ghost && e->modifiers() == Qt::NoModifier) {
        if (e->key() == Qt::Key_R) { rotateSelection(); return; }
        if (e->key() == Qt::Key_M) { mirrorSelection(); return; }
    }
    // device hotkeys: 0-9, a-z, A-Z pick a device for placement
    const QString text = e->text();
    if (!simulating() && text.size() == 1 && Hotkeys::isHotkey(text[0])
        && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        const QString id = Hotkeys::symbolFor(text[0]);
        if (!id.isEmpty()) {
            beginPlace(id);
            if (m_ghost) {
                m_ghostComponent.pos = m_cursor;
                m_ghost->setPos(m_cursor);
            }
            return;
        }
    }
    QGraphicsScene::keyPressEvent(e);
}

void SchematicScene::drawBackground(QPainter* p, const QRectF& rect)
{
    p->fillRect(rect, Qt::white);
    if (m_border)
        drawBorder(p);
    if (!m_gridVisible || m_exporting)
        return;
    // CircuitMaker-style grid: squares three snap steps wide, coarser when
    // zoomed out so lines stay at least 8 px apart
    const double scale = p->worldTransform().m11();
    double step = kGrid * 3;
    while (step * scale < 8)
        step *= 2;
    const double major = step * 5;
    const double x0 = std::floor(rect.left() / step) * step, y0 = std::floor(rect.top() / step) * step;
    auto isMajor = [&](double v) { return std::abs(std::remainder(v, major)) < step / 2; };

    if (m_gridLines) {
        QVector<QLineF> lines;
        for (double x = x0; x < rect.right(); x += step)
            lines << QLineF(x, rect.top(), x, rect.bottom());
        for (double y = y0; y < rect.bottom(); y += step)
            lines << QLineF(rect.left(), y, rect.right(), y);
        p->setPen(QPen(QColor(0xd8, 0xd8, 0xd8), 0));
        p->drawLines(lines);
        return;
    }
    QVector<QPointF> minorDots, majorDots;
    for (double y = y0; y < rect.bottom(); y += step)
        for (double x = x0; x < rect.right(); x += step)
            (isMajor(x) && isMajor(y) ? majorDots : minorDots) << QPointF(x, y);
    QPen pen(QColor(0xa8, 0xac, 0xbc));
    pen.setCosmetic(true);
    pen.setWidthF(1.5);
    pen.setCapStyle(Qt::RoundCap);
    p->setPen(pen);
    p->drawPoints(minorDots.constData(), int(minorDots.size()));
    pen.setColor(QColor(0x70, 0x74, 0x88));
    pen.setWidthF(2.5);
    p->setPen(pen);
    p->drawPoints(majorDots.constData(), int(majorDots.size()));
}

void SchematicScene::drawForeground(QPainter* p, const QRectF&)
{
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0x00, 0x30, 0xff));
    for (QPointF j : std::as_const(m_netlist.junctions))
        p->drawEllipse(j, 4, 4);

    if (!m_wirePoints.isEmpty()) {
        QVector<QPointF> pts = m_wirePoints;
        const QPointF last = pts.last();
        const QPointF corner(m_cursor.x(), last.y());
        pts << corner << m_cursor;
        QPen pen(QColor(0x00, 0x60, 0xff), m_drawingBus ? 4 : 1.4, Qt::DashLine);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawPolyline(pts.constData(), int(pts.size()));
    }
    // probe tool while simulating: a lamp with the node's level next to the cursor
    if (m_sim && m_tool == Tool::Probe && !m_exporting) {
        const sim::Logic v = m_probeNet >= 0 ? m_sim->netValue(m_probeNet) : sim::Logic::Z;
        const QString text = m_probeNet < 0 ? QStringLiteral("-")
                           : v == sim::Logic::L1 ? QStringLiteral("H")
                           : v == sim::Logic::L0 ? QStringLiteral("L")
                           : v == sim::Logic::X ? QStringLiteral("X") : QStringLiteral("Z");
        const QColor fill = m_probeNet < 0 ? QColor(0xd0, 0xd0, 0xd0)
                          : v == sim::Logic::L1 ? QColor(0xff, 0x30, 0x30)
                          : v == sim::Logic::L0 ? QColor(0x30, 0xc0, 0x40)
                          : v == sim::Logic::X ? QColor(0xf0, 0x90, 0x00) : QColor(0xa0, 0xa0, 0xff);
        const QRectF lamp(m_cursor + QPointF(8, -26), QSizeF(18, 18));
        p->setPen(QPen(kInk, 1));
        p->setBrush(fill);
        p->drawEllipse(lamp);
        QFont f = p->font();
        f.setPixelSize(12);
        f.setBold(true);
        p->setFont(f);
        p->drawText(lamp, Qt::AlignCenter, text);
    }
    // pin dots: pins with a wire attached
    if (m_showPinDots) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x00, 0x30, 0xff));
        for (const ComponentItem* item : std::as_const(m_componentItems)) {
            if (item->symbol()->netLabel) continue;
            for (int i = 0; i < item->symbol()->pins.size(); ++i) {
                const int net = m_netlist.netOf(item->id(), i);
                if (net < 0 || m_netlist.nets[net].wires.isEmpty()) continue;
                const QPointF at = m_library.pinScenePos(item->component(), i);
                for (ItemId w : m_netlist.nets[net].wires)
                    if (const Wire* wire = m_circuit->wire(w); wire && WireGeometry::onPolyline(at, wire->points)) {
                        p->drawEllipse(at, 2.5, 2.5);
                        break;
                    }
            }
        }
    }
    // highlighted node (Alt+click)
    if (m_highlightNet >= 0 && m_highlightNet < m_netlist.nets.size()) {
        p->setPen(QPen(QColor(0xff, 0x80, 0x00, 170), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        for (ItemId w : m_netlist.nets[m_highlightNet].wires)
            if (const Wire* wire = m_circuit->wire(w); wire && !wire->bus)
                p->drawPolyline(wire->points.constData(), int(wire->points.size()));
        p->setBrush(QColor(0xff, 0x80, 0x00));
        for (const PinRef& r : m_netlist.nets[m_highlightNet].pins)
            if (const Component* c = m_circuit->component(r.component))
                p->drawEllipse(m_library.pinScenePos(*c, r.pin), 5, 5);
    }
    // node names at the middle of each net's longest wire segment
    if (m_showNodeNames) {
        QHash<int, std::pair<double, QPointF>> best;
        for (const Wire& w : m_circuit->wires()) {
            if (w.bus) continue;
            const int net = m_netlist.wireNet.value(w.id, -1);
            for (int i = 0; i + 1 < w.points.size(); ++i) {
                const double len = QLineF(w.points[i], w.points[i + 1]).length();
                if (len > best.value(net, {-1, {}}).first)
                    best[net] = {len, (w.points[i] + w.points[i + 1]) / 2};
            }
        }
        QFont f = SymbolPainter::schematicFont(10);
        p->setFont(f);
        p->setPen(QColor(0x90, 0x00, 0x90));
        for (auto it = best.cbegin(); it != best.cend(); ++it)
            if (it.key() >= 0)
                p->drawText(it.value().second + QPointF(3, -4), m_netlist.nets[it.key()].name);
    }

    // probes: numbered flags in their trace colours
    const QVector<QPointF>& probes = m_circuit->probes();
    for (int i = 0; i < probes.size(); ++i) {
        const QPointF at = probes[i];
        const QColor c = WaveformView::traceColor(i);
        p->setPen(QPen(c.darker(130), 1.2));
        p->setBrush(c);
        p->drawPolygon(QPolygonF({at, at + QPointF(6, -14), at + QPointF(22, -14), at + QPointF(22, -28), at + QPointF(0, -28)}));
        QFont f;
        f.setPointSizeF(8);
        f.setBold(true);
        p->setFont(f);
        p->setPen(Qt::white);
        p->drawText(QRectF(at.x(), at.y() - 28, 22, 14), Qt::AlignCenter, QString::number(i + 1));
    }
    // operating point: one voltage tag per net, on its first wire or pin
    if (m_overlay && !m_overlay->netVoltage.isEmpty()) {
        QFont f;
        f.setPointSizeF(8);
        p->setFont(f);
        const QFontMetricsF fm(f);
        QSet<int> done;
        auto tag = [&](int net, QPointF at) {
            if (net < 0 || done.contains(net) || !m_overlay->netVoltage.contains(net))
                return;
            done.insert(net);
            const QString text = WaveformView::siFormat(m_overlay->netVoltage.value(net), QStringLiteral("V"));
            const QRectF box(at + QPointF(4, -18), QSizeF(fm.horizontalAdvance(text) + 8, 15));
            p->setPen(QPen(QColor(0xc0, 0x90, 0x10), 0.8));
            p->setBrush(QColor(0xff, 0xf4, 0xc0));
            p->drawRoundedRect(box, 3, 3);
            p->setPen(QColor(0x50, 0x40, 0x00));
            p->drawText(box, Qt::AlignCenter, text);
        };
        for (const WireItem* w : std::as_const(m_wireItems)) {
            const QVector<QPointF>& pts = w->wire().points;
            if (!w->wire().bus && pts.size() >= 2)
                tag(m_netlist.wireNet.value(w->id(), -1), (pts[0] + pts[1]) / 2);
        }
        for (const ComponentItem* item : std::as_const(m_componentItems))
            for (int i = 0; i < item->symbol()->pins.size(); ++i)
                tag(m_netlist.netOf(item->id(), i), m_library.pinScenePos(item->component(), i));
    }

    if ((m_tool == Tool::Wire || m_tool == Tool::Bus || !m_wirePoints.isEmpty()) && isConnectionPoint(m_cursor)) {
        p->setPen(QPen(QColor(0x00, 0xa0, 0x40), 1.2));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(m_cursor, 4, 4);
    }
}

QRectF SchematicScene::itemsRect() const
{
    QRectF r;
    for (QGraphicsItem* it : items())
        if (it != m_ghost)
            r = r.united(it->sceneBoundingRect());
    return r.adjusted(-20, -20, 20, 20);
}

QRectF SchematicScene::contentsRect() const
{
    return m_border ? borderRect().adjusted(-kBorderMargin, -kBorderMargin, kBorderMargin, kBorderMargin) : itemsRect();
}

// Zone frame around the drawing: whole zones, at least 2 x 2.
QRectF SchematicScene::borderRect() const
{
    QRectF r = itemsRect();
    if (r.isEmpty()) r = QRectF(0, 0, 1, 1);
    const double left = std::floor(r.left() / kZone) * kZone, top = std::floor(r.top() / kZone) * kZone;
    const double right = std::max(left + 2 * kZone, std::ceil(r.right() / kZone) * kZone);
    const double bottom = std::max(top + 2 * kZone, std::ceil(r.bottom() / kZone) * kZone);
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

void SchematicScene::setBorderVisible(bool on)
{
    m_border = on;
    updateSceneRect();
    update();
}

// CircuitMaker's map border: zone numbers along the top and bottom, letters
// down the sides.
void SchematicScene::drawBorder(QPainter* p) const
{
    const QRectF inner = borderRect();
    const QRectF outer = inner.adjusted(-kBorderMargin, -kBorderMargin, kBorderMargin, kBorderMargin);
    p->save();
    p->setPen(QPen(kInk, 1.5));
    p->setBrush(Qt::NoBrush);
    p->drawRect(outer);
    p->drawRect(inner);
    QFont f = p->font();
    f.setPixelSize(16);
    p->setFont(f);
    const int cols = qRound(inner.width() / kZone), rows = qRound(inner.height() / kZone);
    for (int i = 0; i < cols; ++i) {
        const double x = inner.left() + i * kZone;
        if (i) {
            p->drawLine(QPointF(x, outer.top()), QPointF(x, inner.top()));
            p->drawLine(QPointF(x, inner.bottom()), QPointF(x, outer.bottom()));
        }
        const QString n = QString::number(i + 1);
        p->drawText(QRectF(x, outer.top(), kZone, kBorderMargin), Qt::AlignCenter, n);
        p->drawText(QRectF(x, inner.bottom(), kZone, kBorderMargin), Qt::AlignCenter, n);
    }
    for (int j = 0; j < rows; ++j) {
        const double y = inner.top() + j * kZone;
        if (j) {
            p->drawLine(QPointF(outer.left(), y), QPointF(inner.left(), y));
            p->drawLine(QPointF(inner.right(), y), QPointF(outer.right(), y));
        }
        QString letter;
        for (int k = j; ; k = k / 26 - 1) {      // A..Z, AA, AB...
            letter.prepend(QChar(u'A' + k % 26));
            if (k < 26) break;
        }
        p->drawText(QRectF(outer.left(), y, kBorderMargin, kZone), Qt::AlignCenter, letter);
        p->drawText(QRectF(inner.right(), y, kBorderMargin, kZone), Qt::AlignCenter, letter);
    }
    p->restore();
}

void SchematicScene::setExporting(bool on)
{
    m_exporting = on;
    update();
}

} // namespace cm
