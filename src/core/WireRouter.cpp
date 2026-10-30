#include "core/WireRouter.h"

#include <QHash>
#include <QSet>

#include <cmath>
#include <algorithm>
#include <climits>
#include <optional>
#include <queue>

namespace cm {

namespace {

constexpr int kBendCost = 6;        // in grid steps
constexpr int kDirs = 4;
const int kDx[kDirs] = {1, 0, -1, 0};
const int kDy[kDirs] = {0, 1, 0, -1};

struct Cell {
    int x, y;
    friend bool operator==(Cell, Cell) = default;
};
inline size_t qHash(Cell c, size_t seed = 0) { return qHashMulti(seed, c.x, c.y); }

std::optional<QVector<QPointF>> search(QPointF a, QPointF b, const RouteObstacles& o, double grid, int margin)
{
    auto cellOf = [&](QPointF p) { return Cell{int(std::lround(p.x() / grid)), int(std::lround(p.y() / grid))}; };
    const Cell start = cellOf(a), goal = cellOf(b);
    const int x0 = std::min(start.x, goal.x) - margin, x1 = std::max(start.x, goal.x) + margin;
    const int y0 = std::min(start.y, goal.y) - margin, y1 = std::max(start.y, goal.y) + margin;
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    auto index = [&](Cell c) { return (c.y - y0) * w + (c.x - x0); };
    auto inside = [&](Cell c) { return c.x >= x0 && c.x <= x1 && c.y >= y0 && c.y <= y1; };

    // blocked cells: device insides and other pins; cells on wires may be
    // crossed but not turned on, and wire edges may not be followed
    QVector<char> blocked(w * h, 0), onWire(w * h, 0);
    QSet<std::pair<int, int>> wireEdges;            // (cell index, direction 0 = +x, 1 = +y)
    for (const QRectF& r : o.bodies) {
        for (int y = std::max(y0, int(std::floor(r.top() / grid)) + 1); y <= std::min(y1, int(std::ceil(r.bottom() / grid)) - 1); ++y)
            for (int x = std::max(x0, int(std::floor(r.left() / grid)) + 1); x <= std::min(x1, int(std::ceil(r.right() / grid)) - 1); ++x)
                blocked[index({x, y})] = 1;
    }
    for (QPointF p : o.pins)
        if (const Cell c = cellOf(p); inside(c) && !(c == start) && !(c == goal)) blocked[index(c)] = 1;
    for (const QLineF& l : o.wires) {
        const Cell s = cellOf(l.p1()), e = cellOf(l.p2());
        const int dx = (e.x > s.x) - (e.x < s.x), dy = (e.y > s.y) - (e.y < s.y);
        if (dx && dy) continue;                     // not orthogonal: ignore
        for (Cell c = s;; c = {c.x + dx, c.y + dy}) {
            if (inside(c)) onWire[index(c)] = 1;
            const Cell n{c.x + dx, c.y + dy};
            if (c == e) break;
            const Cell lo = (dx + dy > 0) ? c : n;
            if (inside(lo)) wireEdges.insert({index(lo), dx ? 0 : 1});
        }
    }
    // a path's own ends may sit on wires (it connects to them) and at the
    // edge of their device's box
    onWire[index(start)] = onWire[index(goal)] = 0;
    blocked[index(start)] = blocked[index(goal)] = 0;

    // A* over (cell, direction of arrival)
    const int states = w * h * kDirs;
    QVector<int> cost(states, INT_MAX), from(states, -1);
    using Item = std::pair<int, int>;               // (estimate, state)
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    auto heuristic = [&](Cell c) { return std::abs(c.x - goal.x) + std::abs(c.y - goal.y); };
    for (int d = 0; d < kDirs; ++d) {
        const int s = index(start) * kDirs + d;
        cost[s] = 0;
        open.push({heuristic(start), s});
    }
    int found = -1;
    while (!open.empty()) {
        const auto [est, s] = open.top();
        open.pop();
        const int ci = s / kDirs, dir = s % kDirs;
        const Cell c{ci % w + x0, ci / w + y0};
        if (est - heuristic(c) > cost[s]) continue;
        if (c == goal) { found = s; break; }
        for (int d = 0; d < kDirs; ++d) {
            const Cell n{c.x + kDx[d], c.y + kDy[d]};
            if (!inside(n) || blocked[index(n)]) continue;
            const bool bend = d != dir && !(c == start);
            if (bend && onWire[ci]) continue;       // a corner on a wire would join it
            const Cell lo = (kDx[d] + kDy[d] > 0) ? c : n;
            if (wireEdges.contains({index(lo), kDx[d] ? 0 : 1})) continue;
            const int ns = index(n) * kDirs + d;
            const int nc = cost[s] + 1 + (bend ? kBendCost : 0);
            if (nc < cost[ns]) {
                cost[ns] = nc;
                from[ns] = s;
                open.push({nc + heuristic(n), ns});
            }
        }
    }
    if (found < 0) return std::nullopt;
    QVector<QPointF> path;
    for (int s = found; s >= 0; s = from[s]) {
        const int ci = s / kDirs;
        const QPointF p((ci % w + x0) * grid, (ci / w + y0) * grid);
        // keep only the corners
        if (path.size() >= 2) {
            const QPointF& a1 = path[path.size() - 2];
            const QPointF& b1 = path.last();
            if ((qFuzzyCompare(a1.x(), b1.x()) && qFuzzyCompare(b1.x(), p.x()))
                || (qFuzzyCompare(a1.y(), b1.y()) && qFuzzyCompare(b1.y(), p.y()))) {
                path.last() = p;
                continue;
            }
        }
        path << p;
    }
    std::reverse(path.begin(), path.end());
    path.first() = a;
    path.last() = b;
    return path;
}

} // namespace

QVector<QPointF> routeWire(QPointF a, QPointF b, const RouteObstacles& obstacles, double grid)
{
    for (int margin : {20, 60})
        if (auto path = search(a, b, obstacles, grid, margin)) return *path;
    return {};
}

} // namespace cm
