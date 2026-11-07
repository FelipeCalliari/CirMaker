#include "ui/TextNoteItem.h"
#include "ui/Theme.h"

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
    if (!m_t.family.isEmpty()) {
        // CircuitMaker's Courier New, or the nearest monospaced face here
        f.setFamilies({m_t.family, QStringLiteral("Liberation Mono"), QStringLiteral("DejaVu Sans Mono"), QStringLiteral("monospace")});
        f.setStyleHint(m_t.family.contains(u"Courier", Qt::CaseInsensitive) ? QFont::TypeWriter : QFont::AnyStyle);
    } else if (!theme().textFont.isEmpty()) {
        f.setFamily(theme().textFont);
    }
    f.setPointSizeF(m_t.pointSize);
    f.setBold(m_t.bold);
    f.setItalic(m_t.italic);
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
    p->setPen(isSelected() ? theme().selection : theme().text);
    p->drawText(m_rect.adjusted(2, 2, -2, -2), Qt::AlignLeft | Qt::AlignTop, m_t.text);
    if (isSelected()) {
        p->setPen(QPen(theme().selection, 0, Qt::DotLine));
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
