#pragma once

#include <QGraphicsView>

namespace cm {

// Wheel zooms around the cursor, middle button pans.
class SchematicView : public QGraphicsView {
    Q_OBJECT
public:
    explicit SchematicView(QGraphicsScene* scene, QWidget* parent = nullptr);

    void zoomBy(double factor);
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
    QPoint m_panStart;
};

} // namespace cm
