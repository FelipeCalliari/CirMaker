#include "ui/SymbolPainter.h"
#include "ui/Theme.h"

#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QRegularExpression>

#include <cmath>

namespace cm::SymbolPainter {

QPainterPath parsePath(const QString& d)
{
    static QHash<QString, QPainterPath> cache;
    if (auto it = cache.constFind(d); it != cache.cend())
        return *it;

    static const QRegularExpression tokenRe(QStringLiteral("[MLQCZ]|-?\\d+(?:\\.\\d+)?"));
    QStringList tokens;
    for (auto it = tokenRe.globalMatch(d); it.hasNext();)
        tokens << it.next().captured();

    QPainterPath path;
    int i = 0;
    auto num = [&]() { return i < tokens.size() ? tokens[i++].toDouble() : 0.0; };
    QChar cmd = u'M';
    while (i < tokens.size()) {
        if (tokens[i].size() == 1 && tokens[i][0].isLetter())
            cmd = tokens[i++][0];
        switch (cmd.unicode()) {
        case u'M': { const double x = num(), y = num(); path.moveTo(x, y); cmd = u'L'; break; }
        case u'L': { const double x = num(), y = num(); path.lineTo(x, y); break; }
        case u'Q': {
            const double cx = num(), cy = num(), x = num(), y = num();
            path.quadTo(cx, cy, x, y);
            break;
        }
        case u'C': {
            const double c1x = num(), c1y = num(), c2x = num(), c2y = num(), x = num(), y = num();
            path.cubicTo(c1x, c1y, c2x, c2y, x, y);
            break;
        }
        case u'Z': path.closeSubpath(); cmd = u'M'; break;
        default: ++i; break;
        }
    }
    cache.insert(d, path);
    return path;
}

QFont schematicFont(double pointSize, bool bold)
{
    QFont f(theme().deviceFont);
    f.setStyleHint(QFont::TypeWriter);
    f.setFamilies({QStringLiteral("Courier New"), QStringLiteral("Liberation Mono"), QStringLiteral("DejaVu Sans Mono"),
                   QStringLiteral("monospace")});
    f.setPointSizeF(pointSize);
    f.setBold(bold);
    return f;
}

void uprightText(QPainter* p, QPointF anchor, const QString& text, double pointSize, int align, const QColor& color,
                 bool overline, bool vcenter)
{
    const QTransform t = p->worldTransform();
    const QPointF at = t.map(anchor);
    const double scale = std::sqrt(std::abs(t.determinant()));

    p->save();
    p->setWorldTransform(QTransform::fromTranslate(at.x(), at.y()).scale(scale, scale));
    QFont f = schematicFont(pointSize);
    f.setOverline(overline);
    p->setFont(f);
    p->setPen(color);
    const QFontMetricsF fm(f);
    const double w = fm.horizontalAdvance(text);
    const double x = align < 0 ? 0 : align > 0 ? -w : -w / 2;
    const double y = vcenter ? (fm.ascent() - fm.descent()) / 2 : 0;
    p->drawText(QPointF(x, y), text);
    p->restore();
}

void paint(QPainter* p, const SymbolDef& s, const QColor& color, QPointF actuatorOffset)
{
    QPen pen(color, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p->setPen(pen);
    for (const Primitive& g : s.graphics) {
        p->setBrush(g.fill ? QBrush(color) : Qt::NoBrush);
        p->save();
        if (g.actuator)
            p->translate(actuatorOffset);
        switch (g.type) {
        case Primitive::Type::Line:
            p->drawLine(QPointF(g.v[0], g.v[1]), QPointF(g.v[2], g.v[3]));
            break;
        case Primitive::Type::Rect:
            p->drawRect(QRectF(g.v[0], g.v[1], g.v[2], g.v[3]));
            break;
        case Primitive::Type::Circle:
            p->drawEllipse(QPointF(g.v[0], g.v[1]), g.v[2], g.v[2]);
            break;
        case Primitive::Type::Path:
            p->drawPath(parsePath(g.path));
            break;
        case Primitive::Type::Text:
            uprightText(p, {g.v[0], g.v[1]}, g.text, g.size, g.align, color, g.overline);
            break;
        }
        p->restore();
    }
}

QPixmap preview(const SymbolDef& s, QSize size, const QColor& color, const QColor& background)
{
    const qreal dpr = 2.0;
    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(background);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF b = s.bounds.adjusted(-4, -4, 4, 4);
    const double scale = std::min({size.width() / b.width(), size.height() / b.height(), 2.5});
    p.translate(size.width() / 2.0, size.height() / 2.0);
    p.scale(scale, scale);
    p.translate(-b.center());
    paint(&p, s, color);
    return pm;
}

} // namespace cm::SymbolPainter
