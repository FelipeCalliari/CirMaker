#pragma once

#include <QPointF>
#include <QVector>

namespace cm::WireGeometry {

bool near(QPointF a, QPointF b, double tolerance = 0.5);
bool onSegment(QPointF p, QPointF a, QPointF b);
bool onPolyline(QPointF p, const QVector<QPointF>& points);

// Drops repeated points and the middle points of straight runs.
QVector<QPointF> simplify(const QVector<QPointF>& points);

// Moves one end of a wire by delta, keeping the segment next to it
// orthogonal (adds an elbow to a two-point wire when needed).
void moveEnd(QVector<QPointF>& points, int end, QPointF delta);

} // namespace cm::WireGeometry
