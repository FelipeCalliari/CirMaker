#pragma once

#include "core/Circuit.h"

#include <QGraphicsItem>

namespace cm {

class TextNoteItem : public QGraphicsItem {
public:
    enum { Type = UserType + 3 };

    explicit TextNoteItem(const TextNote& t);
    void sync(const TextNote& t);
    ItemId id() const { return m_t.id; }
    const TextNote& note() const { return m_t; }

    int type() const override { return Type; }
    QRectF boundingRect() const override { return m_rect; }
    void paint(QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    QFont font() const;
    TextNote m_t;
    QRectF m_rect;
};

} // namespace cm
