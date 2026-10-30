#pragma once

#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QVector>

namespace cm {

// CircuitMaker's auto routing: an orthogonal path on the grid between two
// connection points. Connections here are geometric (a wire end or a pin on
// a wire joins it), so the path must not enter device bodies, touch other
// pins, run along existing wires or bend on them; it may cross wires.
struct RouteObstacles {
    QVector<QRectF> bodies;         // device bodies (their insides are blocked)
    QVector<QPointF> pins;          // pin tips other than the two ends
    QVector<QLineF> wires;          // existing wire segments
};

// The route's vertices from a to b (both on the grid), fewest bends among the
// shortest ones; empty when there is none near the two points.
QVector<QPointF> routeWire(QPointF a, QPointF b, const RouteObstacles& obstacles, double grid);

} // namespace cm
