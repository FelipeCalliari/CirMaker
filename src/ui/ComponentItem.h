#pragma once

#include "core/Circuit.h"

#include <QGraphicsItem>

namespace cm {

struct SymbolDef;
class SchematicScene;

class ComponentItem : public QGraphicsItem {
public:
    enum { Type = UserType + 1 };

    // id 0 makes a placement ghost: drawn translucent, not selectable.
    ComponentItem(const Component& c, const SymbolDef* symbol);

    void sync(const Component& c);
    ItemId id() const { return m_c.id; }
    const Component& component() const { return m_c; }
    const SymbolDef* symbol() const { return m_symbol; }

    // First rectangle of the symbol (its body), or its bounds.
    QRectF bodyRect() const;
    // Which hex-key cell (0..15) is under a local point, or -1.
    int hexKeyAt(QPointF local) const;

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    SchematicScene* schematic() const;
    void paintDynamic(QPainter* p, int state, bool live, const QColor& ink);
    void updateLabelGeometry();

    Component m_c;
    const SymbolDef* m_symbol;
    QRectF m_bounds;
    QPointF m_designatorAt, m_labelAt;  // scene-relative offsets from pos()
    bool m_showDesignator = true, m_showLabel = true;
};

} // namespace cm
