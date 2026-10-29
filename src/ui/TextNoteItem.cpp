#include "ui/TextNoteItem.h"

#include "ui/SchematicScene.h"

#include <QFontMetricsF>
#include <QPainter>

#include <cmath>

namespace cm {

TextNoteItem::TextNoteItem(const TextNote& t)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    sync(t);
}

QFont TextNoteItem::font() const
{
    QFont f;
    f.setPointSizeF(m_t.pointSize);
    return f;
}

void TextNoteItem::sync(const TextNote& t)
{
    prepareGeometryChange();
    m_t = t;
    setPos(t.pos);
    const QFontMetricsF fm(font());
    m_rect = fm.boundingRect(QRectF(0, 0, 2000, 2000), Qt::AlignLeft | Qt::AlignTop, t.text).adjusted(-2, -2, 2, 2);
    update();
}

void TextNoteItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    p->setFont(font());
    p->setPen(isSelected() ? QColor(0x00, 0x60, 0xff) : QColor(0x10, 0x10, 0x30));
    p->drawText(m_rect.adjusted(2, 2, -2, -2), Qt::AlignLeft | Qt::AlignTop, m_t.text);
    if (isSelected()) {
        p->setPen(QPen(QColor(0x00, 0x60, 0xff), 0, Qt::DotLine));
        p->drawRect(m_rect);
    }
}

QVariant TextNoteItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && scene()) {
        const QPointF p = value.toPointF();
        return QPointF(std::round(p.x() / SchematicScene::kGrid) * SchematicScene::kGrid,
                       std::round(p.y() / SchematicScene::kGrid) * SchematicScene::kGrid);
    }
    return QGraphicsItem::itemChange(change, value);
}

} // namespace cm
