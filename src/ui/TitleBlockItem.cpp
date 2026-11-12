#include "ui/TitleBlockItem.h"
#include "ui/Theme.h"

#include "ui/SchematicScene.h"

#include <QPainter>

#include <cmath>

namespace cm {

TitleBlockItem::TitleBlockItem(const TitleBlock& tb)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    sync(tb);
}

void TitleBlockItem::sync(const TitleBlock& tb)
{
    m_tb = tb;
    setPos(tb.pos);
    update();
}

void TitleBlockItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    const QColor ink = isSelected() ? theme().selection : theme().text;
    p->setPen(QPen(ink, 1.6));
    p->setBrush(Qt::NoBrush);
    const QRectF frame(0, 0, kWidth, kHeight);
    p->drawRect(frame);

    const double titleRow = 44, row = (kHeight - titleRow) / 3;
    p->setPen(QPen(ink, 0.8));
    p->drawLine(QPointF(0, titleRow), QPointF(kWidth, titleRow));
    p->drawLine(QPointF(0, titleRow + row), QPointF(kWidth, titleRow + row));
    p->drawLine(QPointF(0, titleRow + 2 * row), QPointF(kWidth, titleRow + 2 * row));

    QFont small;
    small.setPointSizeF(7);
    QFont value;
    value.setPointSizeF(10);
    QFont big;
    big.setPointSizeF(15);
    big.setBold(true);

    p->setPen(ink);
    p->setFont(small);
    p->drawText(QRectF(6, 3, kWidth - 12, 14), Qt::AlignLeft | Qt::AlignTop, tr("Title"));
    p->setFont(big);
    p->drawText(QRectF(6, 12, kWidth - 12, titleRow - 12), Qt::AlignLeft | Qt::AlignVCenter, m_tb.title);

    const QString captions[3] = {tr("Author"), tr("Date"), tr("Revision")};
    const QString values[3] = {m_tb.author, m_tb.date, m_tb.revision};
    for (int i = 0; i < 3; ++i) {
        const QRectF r(6, titleRow + i * row, kWidth - 12, row);
        p->setFont(small);
        p->drawText(r.adjusted(0, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, captions[i]);
        p->setFont(value);
        p->drawText(r.adjusted(70, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, values[i]);
    }
}

QVariant TitleBlockItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && scene()) {
        // a dragged selection moves as one, already on the grid
        if (const auto* s = qobject_cast<const SchematicScene*>(scene()); s && s->movingGroup())
            return value;
        const QPointF p = value.toPointF();
        return QPointF(std::round(p.x() / SchematicScene::kGrid) * SchematicScene::kGrid,
                       std::round(p.y() / SchematicScene::kGrid) * SchematicScene::kGrid);
    }
    return QGraphicsItem::itemChange(change, value);
}

} // namespace cm
