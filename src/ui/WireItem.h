#pragma once

#include "core/Circuit.h"

#include <QGraphicsItem>
#include <QPainterPath>

namespace cm {

class SchematicScene;

// A wire polyline. Points are scene coordinates; while the item is being
// dragged pos() holds the pending offset.
class WireItem : public QGraphicsItem {
public:
    enum { Type = UserType + 2 };

    explicit WireItem(const Wire& w);
    // CircuitMaker's Show Bus Labels: a bus's name in its box (all buses)
    static void setShowBusLabels(bool on);
    static bool showBusLabels();
    void sync(const Wire& w);
    // Temporary shape while connected components are dragged (rubber band).
    void setPreview(const QVector<QPointF>& points);
    void clearPreview();

    ItemId id() const { return m_w.id; }
    const Wire& wire() const { return m_w; }
    // Model points translated by the current drag offset.
    QVector<QPointF> movedPoints() const;

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    void rebuild();

    Wire m_w;
    QVector<QPointF> m_shown;
    QPainterPath m_path, m_shape;
};

} // namespace cm
