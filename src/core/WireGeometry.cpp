#include "core/WireGeometry.h"

#include <QLineF>

#include <algorithm>
#include <cmath>

namespace cm::WireGeometry {

bool near(QPointF a, QPointF b, double tolerance) { return QLineF(a, b).length() <= tolerance; }

bool onSegment(QPointF p, QPointF a, QPointF b)
{
    const QPointF ab = b - a;
    const double len2 = QPointF::dotProduct(ab, ab);
    if (len2 < 1e-9)
        return near(p, a);
    const double t = std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0);
    return near(p, a + t * ab);
}

bool onPolyline(QPointF p, const QVector<QPointF>& points)
{
    for (int i = 0; i + 1 < points.size(); ++i)
        if (onSegment(p, points[i], points[i + 1]))
            return true;
    return points.size() == 1 && near(p, points[0]);
}

QVector<QPointF> simplify(const QVector<QPointF>& in)
{
    QVector<QPointF> out;
    for (QPointF p : in) {
        if (!out.isEmpty() && near(out.last(), p))
            continue;
        if (out.size() >= 2) {
            const QPointF a = out[out.size() - 2], b = out.last();
            const bool collinear = (std::abs(a.x() - b.x()) < 0.5 && std::abs(b.x() - p.x()) < 0.5)
                                || (std::abs(a.y() - b.y()) < 0.5 && std::abs(b.y() - p.y()) < 0.5);
            if (collinear) {
                out.last() = p;
                continue;
            }
        }
        out << p;
    }
    return out;
}

void moveEnd(QVector<QPointF>& pts, int end, QPointF delta)
{
    const QPointF old = pts[end];
    const QPointF moved = old + delta;
    if (pts.size() == 2) {
        const QPointF other = pts[1 - end];
        const bool horizontal = std::abs(old.y() - other.y()) < 0.5;
        pts[end] = moved;
        if (std::abs(moved.x() - other.x()) > 0.5 && std::abs(moved.y() - other.y()) > 0.5) {
            const QPointF corner = horizontal ? QPointF(moved.x(), other.y()) : QPointF(other.x(), moved.y());
            pts.insert(1, corner);
        }
        return;
    }
    const int n = end == 0 ? 1 : int(pts.size()) - 2;
    const bool horizontal = std::abs(old.y() - pts[n].y()) < 0.5;
    if (horizontal)
        pts[n].setY(moved.y());
    else
        pts[n].setX(moved.x());
    pts[end] = moved;
}

} // namespace cm::WireGeometry
