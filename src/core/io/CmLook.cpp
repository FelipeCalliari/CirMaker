#include "core/io/CmLook.h"

#include "core/io/CktExporter.h"
#include "core/io/CmDeviceLib.h"
#include "library/LibraryRegistry.h"
#include "library/SymbolDef.h"

#include <QHash>
#include <QJsonObject>
#include <QRectF>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>

namespace cm::CmLook {

namespace {

constexpr double kScale = 10.0 / 3.0;       // CircuitMaker units to ours

QString num(double v) { return QString::number(std::round(v * 100) / 100, 'g', 8); }

double median(QVector<double> v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

std::optional<bool> g_enabled;

} // namespace

QJsonArray shapeGraphics(const QVector<CmShape>& shapes, const std::function<QPointF(QPoint)>& pt)
{
    // a polyline with a negative count goes on into the next one (a
    // resistor's zigzag, the nonlinear source's hexagon)
    QVector<CmShape> joined;
    for (const CmShape& sh : shapes) {
        if (sh.flags & 0x4000) continue;
        if (!joined.isEmpty() && joined.last().kind == 20 && joined.last().declared < 0 && sh.kind == 20) {
            joined.last().points += sh.points;
            joined.last().declared = sh.declared;
            continue;
        }
        joined << sh;
    }
    // pt is affine (a scale and an offset per axis): points between the grid
    const QPointF o = pt(QPoint(0, 0)), ux = pt(QPoint(1, 0)) - o, uy = pt(QPoint(0, 1)) - o;
    auto ptf = [&](QPointF p) { return o + ux * p.x() + uy * p.y(); };
    QJsonArray graphics;
    for (const CmShape& sh : std::as_const(joined)) {
        if (sh.points.size() < 2) continue;
        if (sh.kind == 11 && sh.points.size() >= 4) {
            // an arc as Windows draws one: the ellipse in the box, counter-clockwise
            // on screen from the line to the third point to the line to the fourth
            const QRectF box = QRectF(QPointF(sh.points[0]), QPointF(sh.points[1])).normalized();
            const QPointF c = box.center();
            const double rx = box.width() / 2, ry = box.height() / 2;
            if (rx <= 0 || ry <= 0) continue;
            auto angle = [&](QPoint p) { return std::atan2(-(p.y() - c.y()) / ry, (p.x() - c.x()) / rx); };
            const double a0 = angle(sh.points[2]);
            double sweep = angle(sh.points[3]) - a0;
            while (sweep <= 1e-9) sweep += 2 * M_PI;
            const int n = int(std::ceil(sweep / (M_PI / 2)));
            const double step = sweep / n, k = 4.0 / 3.0 * std::tan(step / 4);
            auto at = [&](double a) { return QPointF(c.x() + rx * std::cos(a), c.y() - ry * std::sin(a)); };
            auto tangent = [&](double a) { return QPointF(-rx * std::sin(a), -ry * std::cos(a)); };
            QPointF p0 = ptf(at(a0));
            QString path = QStringLiteral("M ") + num(p0.x()) + u' ' + num(p0.y());
            for (int i = 0; i < n; ++i) {
                const double b0 = a0 + i * step, b1 = b0 + step;
                const QPointF c1 = ptf(at(b0) + tangent(b0) * k), c2 = ptf(at(b1) - tangent(b1) * k), e = ptf(at(b1));
                path += QStringLiteral(" C %1 %2 %3 %4 %5 %6").arg(num(c1.x()), num(c1.y()), num(c2.x()), num(c2.y()), num(e.x()), num(e.y()));
            }
            graphics.append(QJsonObject{{QStringLiteral("path"), path}});
            continue;
        }
        if (sh.kind != 0 && sh.kind != 4 && sh.kind != 5 && sh.kind != 6 && sh.kind != 10 && sh.kind != 20) continue;
        const QPointF a = pt(sh.points[0]), b = pt(sh.points[1]);
        const QRectF r = QRectF(a, b).normalized();
        QJsonObject g;
        if (sh.kind == 0) {
            g.insert(QStringLiteral("line"), QJsonArray{a.x(), a.y(), b.x(), b.y()});
        } else if (sh.kind == 5) {
            g.insert(QStringLiteral("rect"), QJsonArray{r.x(), r.y(), r.width(), r.height()});
        } else if (sh.kind == 4) {
            // an ellipse in four quarter curves
            const double k = 0.5523, cx = r.center().x(), cy = r.center().y(), rx = r.width() / 2, ry = r.height() / 2;
            g.insert(QStringLiteral("path"), QStringLiteral("M %1 %2 C %3 %4 %5 %6 %7 %8 C %9 %10 %11 %12 %13 %14 ")
                                  .arg(num(cx + rx), num(cy), num(cx + rx), num(cy + k * ry), num(cx + k * rx), num(cy + ry), num(cx), num(cy + ry))
                                  .arg(num(cx - k * rx), num(cy + ry), num(cx - rx), num(cy + k * ry), num(cx - rx), num(cy))
                              + QStringLiteral("C %1 %2 %3 %4 %5 %6 C %7 %8 %9 %10 %11 %12 Z")
                                  .arg(num(cx - rx), num(cy - k * ry), num(cx - k * rx), num(cy - ry), num(cx), num(cy - ry))
                                  .arg(num(cx + k * rx), num(cy - ry), num(cx + rx), num(cy - k * ry), num(cx + rx), num(cy)));
        } else if (sh.kind == 6) {
            // rounded corners: the third point holds the radii
            const QPointF rad = sh.points.size() > 2 ? pt(sh.points[2]) / 2 : QPointF();
            const double rx = std::min(rad.x(), r.width() / 2), ry = std::min(rad.y(), r.height() / 2);
            const double l = r.left(), t = r.top(), rr = r.right(), bb = r.bottom();
            g.insert(QStringLiteral("path"), QStringLiteral("M %1 %2 L %3 %2 Q %4 %2 %4 %5 L %4 %6 Q %4 %7 %3 %7 L %1 %7 Q %8 %7 %8 %6 L %8 %5 Q %8 %2 %1 %2 Z")
                                  .arg(num(l + rx), num(t), num(rr - rx), num(rr), num(t + ry), num(bb - ry), num(bb), num(l)));
        } else {
            QString path;
            for (int k = 0; k < sh.points.size(); ++k) {
                const QPointF q = pt(sh.points[k]);
                path += (k ? QStringLiteral(" L ") : QStringLiteral("M ")) + num(q.x()) + u' ' + num(q.y());
            }
            if (sh.kind == 10) path += QStringLiteral(" Z");
            g.insert(QStringLiteral("path"), path);
        }
        if (sh.flags & 0x1000) g.insert(QStringLiteral("fill"), true);
        graphics.append(g);
    }
    return graphics;
}

const CmDeviceLib& deviceLib()
{
    static const CmDeviceLib lib = [] {
        CmDeviceLib l;
        const QString path = CmDeviceLib::locate(QSettings().value(QStringLiteral("cm/folder")).toString());
        if (!path.isEmpty()) l.load(path);
        return l;
    }();
    return lib;
}

bool enabled()
{
    if (!g_enabled) g_enabled = QSettings().value(QStringLiteral("view/cmDrawings"), true).toBool();
    return *g_enabled;
}

void setEnabled(bool on)
{
    g_enabled = on;
    QSettings().setValue(QStringLiteral("view/cmDrawings"), on);
}

const SymbolDef* symbolFor(const SymbolDef& s, const QString& cmDevice)
{
    // drawn live, or a label: our own drawing
    if (!enabled() || !s.display.isEmpty() || s.netLabel || s.pins.isEmpty()) return &s;
    const CmDeviceLib& lib = deviceLib();
    if (!lib.isLoaded()) return &s;
    const CmLibDevice* dev = nullptr;
    if (!cmDevice.isEmpty()) dev = lib.find(cmDevice);
    if (!dev) dev = CktExporter::deviceFor(s, lib);
    // a drawing of its own (CircuitMaker draws +V and the like itself)
    if (!dev || dev->pinCount != dev->pins.size()
        || std::none_of(dev->drawing.cbegin(), dev->drawing.cend(), [](const CmShape& sh) { return !(sh.flags & 0x4000); })
        // records of a kind not worked out (12: the Signal Gen's, each waveform drawn over the others)
        || std::any_of(dev->drawing.cbegin(), dev->drawing.cend(), [](const CmShape& sh) {
               return sh.kind != 0 && sh.kind != 4 && sh.kind != 5 && sh.kind != 6 && sh.kind != 10 && sh.kind != 11 && sh.kind != 20; }))
        return &s;

    static QHash<QString, std::shared_ptr<SymbolDef>> cache;    // null: not drawn as CircuitMaker's
    const QString key = s.id + u'|' + dev->name;
    if (auto it = cache.constFind(key); it != cache.cend()) return it.value() ? it.value().get() : &s;
    cache.insert(key, nullptr);
    static const bool debug = qEnvironmentVariableIsSet("CM_LOOK_DEBUG");
    if (debug) std::fprintf(stderr, "cmlook %s as %s" "\n", qPrintable(s.id), qPrintable(dev->name));

    // which of our pins each CircuitMaker pin is
    bool numbered = false;
    for (const SymbolPin& p : s.pins) numbered = numbered || !p.number.isEmpty();
    // a "pin" pointing nowhere ('x') is a mark drawn by the part: V Source's + and -
    auto mark = [dev](int k) { return dev->pinStyle.value(k).size() > 6 && dev->pinStyle[k].at(6) == 'x'; };
    QVector<int> ours(dev->pinCount, -1);
    QVector<bool> covered(s.pins.size(), false);
    for (int k = 1; k <= dev->pinCount; ++k) {
        if (mark(k - 1)) continue;
        int i = -1;
        if (!s.cmPins.isEmpty()) {
            i = s.pinIndex(s.cmPins.value(k - 1));
        } else if (numbered) {
            const QString number = QString::number(dev->packageMap.size() > dev->pinCount ? dev->packageMap[k] : k);
            for (int j = 0; j < s.pins.size(); ++j)
                if (s.pins[j].number == number) i = j;
        } else if (k - 1 < s.pins.size()) {
            i = k - 1;
        }
        if (i < 0 || covered[i]) return &s;         // a pin CircuitMaker draws that we lack
        ours[k - 1] = i;
        covered[i] = true;
    }
    if (std::find(covered.cbegin(), covered.cend(), false) != covered.cend()) return &s;

    // CircuitMaker's pins onto ours. A pin may lie anywhere out along its
    // lead (ours are often longer), so each axis is fitted from the pins
    // whose leads run across it: a scale near ours and an offset
    auto dirOf = [dev](int k) { return dev->pinStyle.value(k).size() > 6 ? dev->pinStyle[k].at(6) : '\0'; };
    auto fit = [&](bool xAxis, double& scale, double& offset) {
        QVector<double> cmV, ourV, all;
        for (int k = 0; k < dev->pinCount; ++k) {
            if (ours[k] < 0) continue;
            const char d = dirOf(k);
            const bool across = xAxis ? (d == 'u' || d == 'd') : (d == 'l' || d == 'r');
            const double c = xAxis ? dev->pins[k].x() : dev->pins[k].y(), o = xAxis ? s.pins[ours[k]].pos.x() : s.pins[ours[k]].pos.y();
            all << o - c * kScale;
            if (across) { cmV << c; ourV << o; }
        }
        scale = kScale;
        // two or more places across: their spread sets the scale
        const auto [lo, hi] = std::minmax_element(cmV.cbegin(), cmV.cend());
        if (cmV.size() >= 2 && *hi - *lo >= 6) {
            double mc = 0, mo = 0;
            for (int i = 0; i < cmV.size(); ++i) { mc += cmV[i]; mo += ourV[i]; }
            mc /= cmV.size(); mo /= cmV.size();
            double num = 0, den = 0;
            for (int i = 0; i < cmV.size(); ++i) { num += (cmV[i] - mc) * (ourV[i] - mo); den += (cmV[i] - mc) * (cmV[i] - mc); }
            scale = std::clamp(num / den, kScale * 0.85, kScale * 1.2);
            offset = mo - scale * mc;
            return;
        }
        QVector<double> d;
        for (int i = 0; i < cmV.size(); ++i) d << ourV[i] - cmV[i] * kScale;
        offset = median(d.isEmpty() ? all : d);
    };
    double sx, ox, sy, oy;
    fit(true, sx, ox);
    fit(false, sy, oy);
    auto pt = [&](QPoint p) { return QPointF(p.x() * sx + ox, p.y() * sy + oy); };
    for (int k = 0; k < dev->pinCount; ++k) {
        if (ours[k] < 0) continue;
        const QPointF from = pt(dev->pinFrom.value(k, dev->pins[k])), at = pt(dev->pins[k]), to = s.pins[ours[k]].pos;
        const char d = dirOf(k);
        const bool horizontal = d == 'l' || d == 'r', vertical = d == 'u' || d == 'd';
        // across the lead within a quarter grid; along it, outward of where it starts
        const double across = horizontal ? to.y() - at.y() : vertical ? to.x() - at.x() : (to - at).manhattanLength();
        const double out = d == 'l' ? from.x() - to.x() : d == 'r' ? to.x() - from.x() : d == 'u' ? from.y() - to.y() : d == 'd' ? to.y() - from.y() : 0;
        const double along = horizontal ? to.x() - at.x() : vertical ? to.y() - at.y() : 0;
        if (std::abs(across) > 2.5 || out < 0 || (!horizontal && !vertical && std::abs(along) > 2.5)) {
            if (debug) std::fprintf(stderr, "  pin %d misses by %g across, %g out (scale %g %g)\n", k + 1, across, out, sx, sy);
            return &s;
        }
    }

    QJsonArray graphics = shapeGraphics(dev->drawing, pt);
    bool labelInside = false;
    const QPointF o = pt(QPoint(0, 0)), ux = pt(QPoint(1, 0)) - o, uy = pt(QPoint(0, 1)) - o;
    auto ptf = [&](QPointF p) { return o + ux * p.x() + uy * p.y(); };
    // flag 0x80: the part's own label inside the top of the body (a chip's
    // part number, ADC8, a SCOPE's trace name; not the 555, whose label is
    // shown outside instead)
    if ((dev->flags & 0x80) && !dev->value.trimmed().isEmpty()) {
        QRect body;
        for (const CmShape& sh : dev->drawing)
            if (!(sh.flags & 0x4000))
                for (QPoint q : sh.points) body |= QRect(q, QSize(1, 1));
        const QPointF at = ptf(QPointF(body.center().x() + 0.5, body.top() + 5));
        graphics.append(QJsonObject{{QStringLiteral("text"), QString()}, {QStringLiteral("label"), true},
                                    {QStringLiteral("at"), QJsonArray{at.x(), at.y()}}, {QStringLiteral("size"), 14}});
        labelInside = true;
    }
    // each pin's lead, straight out to our pin
    for (int k = 0; k < dev->pinCount; ++k) {
        if (ours[k] < 0) {
            // a mark: its text, from the bottom left corner of CircuitMaker's
            if (const QString t = dev->pinNames.value(k).trimmed(); !t.isEmpty())
                graphics.append(QJsonObject{{QStringLiteral("text"), t}, {QStringLiteral("at"), QJsonArray{pt(dev->pinFrom.value(k) + QPoint(2, -4)).x(),
                                                                                                    pt(dev->pinFrom.value(k) + QPoint(2, -4)).y()}},
                                            {QStringLiteral("size"), 8}});
            continue;
        }
        const QPointF to = s.pins[ours[k]].pos;
        const QPoint start = dev->pinFrom.value(k, dev->pins[k]);
        QPointF from = pt(start);
        const char dir = dirOf(k);
        const QPoint step = dir == 'l' ? QPoint(-1, 0) : dir == 'r' ? QPoint(1, 0) : dir == 'u' ? QPoint(0, -1) : dir == 'd' ? QPoint(0, 1) : QPoint();
        if (dir == 'l' || dir == 'r') from.setY(to.y());
        else if (dir == 'u' || dir == 'd') from.setX(to.x());
        // '+': an inverting pin, a bubble against the body and the lead beyond
        if (dev->pinStyle.value(k).startsWith('+') && !step.isNull()) {
            const QPointF c = ptf(QPointF(start) + QPointF(step) * 3);
            const QPointF r(std::abs(ux.x()) * 2.5, std::abs(uy.y()) * 2.5);
            graphics.append(QJsonObject{{QStringLiteral("circle"), QJsonArray{c.x(), c.y(), (r.x() + r.y()) / 2}}});
            from = ptf(QPointF(start) + QPointF(step) * 6);
            if (dir == 'l' || dir == 'r') from.setY(to.y());
            else from.setX(to.x());
        }
        if (from != to) graphics.append(QJsonObject{{QStringLiteral("line"), QJsonArray{from.x(), from.y(), to.x(), to.y()}}});
        // flag 0x1000: the part names its pins inside, by each one (chips, the 555, .IC)
        if ((dev->flags & 0x1000) && !step.isNull()) {
            if (QString name = dev->pinNames.value(k).trimmed(); !name.isEmpty()) {
                // "{Q3}": Q3 with a bar over it
                const bool bar = name.size() > 2 && ((name.startsWith(u'{') && name.endsWith(u'}')) || (name.startsWith(u'(') && name.endsWith(u')')));
                if (bar) name = name.mid(1, name.size() - 2);
                const QPointF at = ptf(QPointF(start) - QPointF(step) * 1.5 + (step.x() ? QPointF(0, 0) : QPointF(0, step.y() > 0 ? -4 : 4)));
                graphics.append(QJsonObject{{QStringLiteral("text"), name}, {QStringLiteral("at"), QJsonArray{at.x(), at.y()}}, {QStringLiteral("size"), 13},
                                            {QStringLiteral("turns"), true}, {QStringLiteral("overline"), bar},
                                            {QStringLiteral("align"), dir == 'l' ? QStringLiteral("left") : dir == 'r' ? QStringLiteral("right")
                                                                                                              : QStringLiteral("center")}});
            }
        }
    }
    QJsonArray pins;
    for (const SymbolPin& p : s.pins) pins.append(QJsonObject{{QStringLiteral("name"), p.name}, {QStringLiteral("at"), QJsonArray{p.pos.x(), p.pos.y()}}});
    SymbolDef drawn;
    if (!LibraryRegistry::parseSymbol(QJsonObject{{QStringLiteral("id"), s.id}, {QStringLiteral("name"), s.name},
                                                  {QStringLiteral("graphics"), graphics}, {QStringLiteral("pins"), pins}},
                                      drawn, nullptr))
        return &s;
    auto out = std::make_shared<SymbolDef>(s);
    out->graphics = drawn.graphics;
    out->bounds = drawn.bounds;
    out->labelInside = labelInside;
    cache.insert(key, out);
    return out.get();
}

} // namespace cm::CmLook
