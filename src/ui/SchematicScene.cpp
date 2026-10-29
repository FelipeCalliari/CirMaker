#include "ui/SchematicScene.h"

#include "core/WireGeometry.h"
#include "core/io/NativeFormat.h"
#include "library/LibraryRegistry.h"
#include "sim/digital/EventSimulator.h"
#include "ui/ComponentItem.h"
#include "ui/PropertiesDialog.h"
#include "ui/TextNoteItem.h"
#include "ui/WireItem.h"

#include <QApplication>
#include <QClipboard>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
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
    connect(circuit, &Circuit::modified, this, &SchematicScene::scheduleNetlist);

    m_timer.setInterval(30);
    connect(&m_timer, &QTimer::timeout, this, &SchematicScene::advance);

    for (ItemId id : circuit->components().keys()) onComponentAdded(id);
    for (ItemId id : circuit->wires().keys()) onWireAdded(id);
    for (ItemId id : circuit->texts().keys()) onTextAdded(id);
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
}

void SchematicScene::scheduleNetlist()
{
    if (m_netlistPending)
        return;
    m_netlistPending = true;
    QTimer::singleShot(0, this, &SchematicScene::rebuildNetlist);
}

void SchematicScene::rebuildNetlist()
{
    m_netlistPending = false;
    m_netlist = NetlistBuilder::build(*m_circuit, m_library);
    update();
}

// ---------------------------------------------------------------- tools

void SchematicScene::setTool(Tool tool)
{
    if (tool != Tool::Place && m_ghost)
        cancelPlace();
    if (!m_wirePoints.isEmpty())
        cancelWire();
    m_tool = tool;
    for (QGraphicsView* v : views())
        v->viewport()->setCursor(tool == Tool::Select ? Qt::ArrowCursor
                                 : tool == Tool::Text ? Qt::IBeamCursor : Qt::CrossCursor);
    emit toolChanged(tool);
}

void SchematicScene::beginPlace(const QString& symbolId)
{
    const SymbolDef* s = m_library.find(symbolId);
    if (!s || simulating())
        return;
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
    m_wirePoints.clear();
    m_wireFromSelect = false;
    if (pts.size() >= 2) {
        Selection sel;
        sel.wires << Wire{0, pts};
        m_undo->push(new AddItemsCommand(m_circuit, sel, tr("Add wire")));
    }
    update();
}

void SchematicScene::cancelWire()
{
    m_wirePoints.clear();
    m_wireFromSelect = false;
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
    }
    m_drag.active = !m_drag.componentStart.isEmpty() || !m_drag.textStart.isEmpty() || !m_drag.wires.isEmpty();
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
    if (!after.isEmpty())
        m_undo->push(new ModifyItemsCommand(m_circuit, before, after, tr("Move")));
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
    copySelection();
    const Selection sel = selection();
    if (!sel.isEmpty())
        m_undo->push(new RemoveItemsCommand(m_circuit, sel, tr("Cut")));
}

void SchematicScene::paste()
{
    if (simulating())
        return;
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(kMime))
        return;
    Circuit temp;
    if (!NativeFormat::deserialize(mime->data(kMime), temp, nullptr))
        return;
    Selection sel = fromCircuit(temp);
    const QPointF offset(20, 20);
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
    auto* cmd = new AddItemsCommand(m_circuit, sel, tr("Paste"));
    m_undo->push(cmd);
    clearSelection();
    for (const Component& c : cmd->items().components)
        if (auto* it = m_componentItems.value(c.id)) it->setSelected(true);
    for (const Wire& w : cmd->items().wires)
        if (auto* it = m_wireItems.value(w.id)) it->setSelected(true);
    for (const TextNote& t : cmd->items().texts)
        if (auto* it = m_textItems.value(t.id)) it->setSelected(true);
}

void SchematicScene::editItem(QGraphicsItem* item)
{
    if (simulating() || !item)
        return;
    if (auto* c = qgraphicsitem_cast<ComponentItem*>(item)) {
        const Component before = *m_circuit->component(c->id());
        PropertiesDialog dlg(before, *c->symbol(), dialogParent());
        if (dlg.exec() != QDialog::Accepted)
            return;
        const Component after = dlg.result();
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

// ---------------------------------------------------------------- simulation

void SchematicScene::run()
{
    if (!m_sim) {
        setTool(Tool::Select);
        clearSelection();
        rebuildNetlist();
        m_sim = std::make_unique<sim::EventSimulator>();
        m_sim->build(*m_circuit, m_library, m_netlist);
        m_sim->reset();
        if (!m_sim->warnings().isEmpty())
            emit statusMessage(tr("Simulation: %n problem(s); first: %1", nullptr, int(m_sim->warnings().size()))
                                   .arg(m_sim->warnings().first()));
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
    update();
    emit simulationChanged();
}

void SchematicScene::persistSimProps()
{
    Selection before, after;
    for (const Component& c : m_circuit->components()) {
        const QVariantMap p = m_sim->persistentProps(c.id);
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
    m_sim->step(m_ticksPerFrame);
    emit simTimeChanged(m_sim->time());
    update();
}

quint64 SchematicScene::simTime() const { return m_sim ? m_sim->time() : 0; }

int SchematicScene::simState(ItemId id) const { return m_sim ? m_sim->componentState(id) : 0; }

QColor SchematicScene::wireColor(ItemId id) const
{
    if (!m_sim || m_exporting)
        return kInk;
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
        if (e->button() == Qt::LeftButton)
            simInteractPress(e);
        return;
    }
    if (e->button() == Qt::RightButton) {
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
        wireClick(p);
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
    if (!(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) && pinAt(e->scenePos(), &pin)) {
        clearSelection();
        m_wireFromSelect = true;
        m_wirePoints = {pin};
        update();
        return;
    }
    QGraphicsScene::mousePressEvent(e);
    beginDrag();
}

void SchematicScene::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    m_cursor = snap(e->scenePos());
    emit cursorMoved(m_cursor);
    if (simulating())
        return;
    if (m_ghost) {
        m_ghostComponent.pos = m_cursor;
        m_ghost->setPos(m_cursor);
        return;
    }
    if (!m_wirePoints.isEmpty()) {
        update();
        return;
    }
    QGraphicsScene::mouseMoveEvent(e);
    if (m_drag.active && (e->buttons() & Qt::LeftButton))
        updateDragPreview();
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
        if (it->type() == ComponentItem::Type || it->type() == TextNoteItem::Type) {
            if (it == m_ghost)
                continue;
            editItem(it);
            return;
        }
    }
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
    if (m_ghost && e->modifiers() == Qt::NoModifier) {
        if (e->key() == Qt::Key_R) { rotateSelection(); return; }
        if (e->key() == Qt::Key_M) { mirrorSelection(); return; }
    }
    QGraphicsScene::keyPressEvent(e);
}

void SchematicScene::drawBackground(QPainter* p, const QRectF& rect)
{
    p->fillRect(rect, Qt::white);
    if (!m_gridVisible || m_exporting)
        return;
    const double scale = p->worldTransform().m11();
    double step = kGrid;
    while (step * scale < 7)
        step *= 5;
    QVector<QPointF> dots;
    const double x0 = std::floor(rect.left() / step) * step, y0 = std::floor(rect.top() / step) * step;
    for (double y = y0; y < rect.bottom(); y += step)
        for (double x = x0; x < rect.right(); x += step)
            dots << QPointF(x, y);
    p->setPen(QPen(QColor(0xb8, 0xb8, 0xc8), 0));
    p->drawPoints(dots.constData(), int(dots.size()));
}

void SchematicScene::drawForeground(QPainter* p, const QRectF&)
{
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(Qt::NoPen);
    p->setBrush(kInk);
    for (QPointF j : std::as_const(m_netlist.junctions))
        p->drawEllipse(j, 2.6, 2.6);

    if (!m_wirePoints.isEmpty()) {
        QVector<QPointF> pts = m_wirePoints;
        const QPointF last = pts.last();
        const QPointF corner(m_cursor.x(), last.y());
        pts << corner << m_cursor;
        QPen pen(QColor(0x00, 0x60, 0xff), 1.4, Qt::DashLine);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawPolyline(pts.constData(), int(pts.size()));
    }
    if ((m_tool == Tool::Wire || !m_wirePoints.isEmpty()) && isConnectionPoint(m_cursor)) {
        p->setPen(QPen(QColor(0x00, 0xa0, 0x40), 1.2));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(m_cursor, 4, 4);
    }
}

QRectF SchematicScene::contentsRect() const
{
    QRectF r;
    for (QGraphicsItem* it : items())
        if (it != m_ghost)
            r = r.united(it->sceneBoundingRect());
    return r.adjusted(-20, -20, 20, 20);
}

void SchematicScene::setExporting(bool on)
{
    m_exporting = on;
    update();
}

} // namespace cm
