#include "ui/WireItem.h"
#include "ui/Theme.h"

#include "ui/SchematicScene.h"
#include "ui/SymbolPainter.h"

#include <QFontMetricsF>

#include <QPainter>
#include <QPainterPathStroker>

#include <cmath>

namespace cm {

WireItem::WireItem(const Wire& w)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setZValue(-1);
    sync(w);
}

void WireItem::sync(const Wire& w)
{
    m_w = w;
    m_shown = w.points;
    setPos(0, 0);
    rebuild();
}

void WireItem::setPreview(const QVector<QPointF>& points)
{
    m_shown = points;
    rebuild();
}

void WireItem::clearPreview()
{
    m_shown = m_w.points;
    rebuild();
}

QVector<QPointF> WireItem::movedPoints() const
{
    QVector<QPointF> out = m_w.points;
    for (QPointF& p : out)
        p += pos();
    return out;
}

void WireItem::rebuild()
{
    prepareGeometryChange();
    m_path = QPainterPath();
    if (!m_shown.isEmpty()) {
        m_path.moveTo(m_shown.first());
        for (int i = 1; i < m_shown.size(); ++i)
            m_path.lineTo(m_shown[i]);
    }
    QPainterPathStroker stroker;
    stroker.setWidth(6);
    m_shape = stroker.createStroke(m_path);
    update();
}

QRectF WireItem::boundingRect() const
{
    const double margin = m_w.bus && !m_w.name.isEmpty() ? 40 : 3;
    return m_shape.boundingRect().adjusted(-margin, -margin, margin, margin);
}

QPainterPath WireItem::shape() const { return m_shape; }

void WireItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    const SchematicScene* sc = qobject_cast<SchematicScene*>(scene());
    QColor color = sc && sc->simulating() ? sc->wireColor(m_w.id) : theme().wire;
    if (m_w.bus)
        color = theme().bus;
    QPen pen(color, m_w.bus ? 8.0 : 2.6);     // wires heavier than the parts, as in CircuitMaker
    if (isSelected()) {
        pen.setColor(theme().selection);
        pen.setWidthF(m_w.bus ? 8.0 : 2.2);
    } else if (sc && sc->simulating() && !m_w.bus) {
        pen.setWidthF(2.0);
    }
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(pen);
    p->drawPath(m_path);

    // bus name in a box past the bus's top (or left) end
    if (m_w.bus && !m_w.name.isEmpty() && m_shown.size() >= 2) {
        QPointF end = m_shown.first(), next = m_shown[1];
        if (m_shown.last().y() < end.y() || (qFuzzyCompare(m_shown.last().y(), end.y()) && m_shown.last().x() < end.x())) {
            end = m_shown.last();
            next = m_shown[m_shown.size() - 2];
        }
        const QPointF dir = QLineF(next, end).unitVector().p2() - next;
        QFont f = SymbolPainter::schematicFont(14);
        p->setFont(f);
        const QFontMetricsF fm(f);
        const QSizeF size(fm.horizontalAdvance(m_w.name) + 10, fm.height() + 4);
        const QPointF centre = end + dir * (size.height() / 2 + 2);
        const QRectF box(centre - QPointF(size.width() / 2, size.height() / 2), size);
        p->setPen(QPen(theme().bus, 1.4));
        p->setBrush(theme().background);
        p->drawRect(box);
        p->drawText(box, Qt::AlignCenter, m_w.name);
    }
}

QVariant WireItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && scene()) {
        const QPointF p = value.toPointF();
        return QPointF(std::round(p.x() / SchematicScene::kGrid) * SchematicScene::kGrid,
                       std::round(p.y() / SchematicScene::kGrid) * SchematicScene::kGrid);
    }
    return QGraphicsItem::itemChange(change, value);
}

} // namespace cm
