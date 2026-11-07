#pragma once

#include <optional>

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
    bool singleKey() const;
    QRectF keyFace() const;
    int keyRow(int row) const;
    // Designator (0) or label (1) text under a scene point, -1 if none; its
    // anchor as stored in Component::designatorAt/labelAt (centre of the
    // baseline, scene orientation, relative to pos()).
    int textAt(QPointF scenePos) const;
    QPointF textAnchor(int which) const;
    // drawn at this anchor while dragged; nullopt ends the preview
    void setTextPreview(int which, std::optional<QPointF> anchor);

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
    int m_previewText = -1;
    QPointF m_previewAt;
    QRectF textRect(int which) const;   // scene orientation, relative to pos()
    bool m_showDesignator = true, m_showLabel = true;
};

} // namespace cm
