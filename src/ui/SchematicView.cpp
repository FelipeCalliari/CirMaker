#include "ui/SchematicView.h"

#include "ui/SchematicScene.h"

#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>

namespace cm {

SchematicView::SchematicView(QGraphicsScene* scene, QWidget* parent) : QGraphicsView(scene, parent)
{
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::TextAntialiasing);
    setDragMode(RubberBandDrag);
    setRubberBandSelectionMode(Qt::IntersectsItemShape);
    setTransformationAnchor(AnchorUnderMouse);
    setViewportUpdateMode(SmartViewportUpdate);
    setMouseTracking(true);
    setTransform(QTransform::fromScale(m_defaultZoom, m_defaultZoom));
}

void SchematicView::zoomBy(double factor)
{
    const double z = zoom() * factor;
    if (z < 0.2 || z > 20)
        return;
    scale(factor, factor);
    emit zoomChanged(zoom());
}

void SchematicView::zoomToFit()
{
    auto* s = qobject_cast<SchematicScene*>(scene());
    QRectF r = s ? s->contentsRect() : scene()->itemsBoundingRect();
    if (r.width() < 200 || r.height() < 150)
        r = QRectF(r.center() - QPointF(100, 75), QSizeF(200, 150)) | r;
    fitInView(r, Qt::KeepAspectRatio);
    if (zoom() > m_defaultZoom) {
        setTransform(QTransform::fromScale(m_defaultZoom, m_defaultZoom));
        centerOn(r.center());
    }
    emit zoomChanged(zoom());
}

void SchematicView::resetZoom()
{
    setTransform(QTransform::fromScale(m_defaultZoom, m_defaultZoom));
    emit zoomChanged(zoom());
}

void SchematicView::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & (Qt::ShiftModifier | Qt::AltModifier))
        return QGraphicsView::wheelEvent(e);
    const double steps = e->angleDelta().y() / 120.0;
    zoomBy(std::pow(1.2, steps));
    e->accept();
}

void SchematicView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStart = e->position().toPoint();
        viewport()->setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    // rubber-band selection only makes sense with the select tool
    auto* s = qobject_cast<SchematicScene*>(scene());
    setDragMode(s && s->tool() == SchematicScene::Tool::Select && !s->simulating() ? RubberBandDrag : NoDrag);
    QGraphicsView::mousePressEvent(e);
}

void SchematicView::mouseMoveEvent(QMouseEvent* e)
{
    if (m_panning) {
        const QPoint d = e->position().toPoint() - m_panStart;
        m_panStart = e->position().toPoint();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void SchematicView::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_panning && e->button() == Qt::MiddleButton) {
        m_panning = false;
        auto* s = qobject_cast<SchematicScene*>(scene());
        viewport()->setCursor(s && s->tool() != SchematicScene::Tool::Select ? Qt::CrossCursor : Qt::ArrowCursor);
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

} // namespace cm
