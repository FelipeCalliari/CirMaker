#include "ui/WireItem.h"

#include "ui/SchematicScene.h"

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

QRectF WireItem::boundingRect() const { return m_shape.boundingRect().adjusted(-2, -2, 2, 2); }

QPainterPath WireItem::shape() const { return m_shape; }

void WireItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    const SchematicScene* sc = qobject_cast<SchematicScene*>(scene());
    QColor color = sc ? sc->wireColor(m_w.id) : QColor(0x10, 0x10, 0x30);
    QPen pen(color, m_w.bus ? 4.0 : 1.4);
    if (isSelected()) {
        pen.setColor(QColor(0x00, 0x60, 0xff));
        pen.setWidthF(2.2);
    } else if (sc && sc->simulating()) {
        pen.setWidthF(2.0);
    }
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(pen);
    p->drawPath(m_path);
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
