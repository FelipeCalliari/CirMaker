#pragma once

#include <QGraphicsView>

namespace cm {

// Wheel zooms around the cursor, middle button pans.
class SchematicView : public QGraphicsView {
    Q_OBJECT
public:
    explicit SchematicView(QGraphicsScene* scene, QWidget* parent = nullptr);

    void zoomBy(double factor);
    // Initial zoom; "fit to window" never enlarges beyond it.
    void setDefaultZoom(double zoom) { m_defaultZoom = zoom; }
    void zoomToFit();
    void resetZoom();
    double zoom() const { return transform().m11(); }

signals:
    void zoomChanged(double zoom);

protected:
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    bool m_panning = false;
    double m_defaultZoom = 0.6;
    QPoint m_panStart;
};

} // namespace cm
