#pragma once

#include "core/Circuit.h"

#include <QCoreApplication>
#include <QGraphicsItem>

namespace cm {

// The drawing's title block: title, author, date and revision in a framed table.
class TitleBlockItem : public QGraphicsItem {
    Q_DECLARE_TR_FUNCTIONS(TitleBlockItem)
public:
    enum { Type = UserType + 4 };
    static constexpr double kWidth = 340, kHeight = 110;

    explicit TitleBlockItem(const TitleBlock& tb);
    void sync(const TitleBlock& tb);

    int type() const override { return Type; }
    QRectF boundingRect() const override { return QRectF(-2, -2, kWidth + 4, kHeight + 4); }
    void paint(QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    TitleBlock m_tb;
};

} // namespace cm
