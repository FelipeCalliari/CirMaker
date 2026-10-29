#include "ui/ComponentItem.h"

#include "library/SymbolDef.h"
#include "ui/SchematicScene.h"
#include "ui/SymbolPainter.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <climits>
#include <cmath>

namespace cm {

namespace {

constexpr int kIdle = INT_MIN;     // not simulating: displays stay blank

const QColor kInk(0x00, 0x00, 0x00);
const QColor kSelected(0x00, 0x60, 0xff);
const QColor kHigh(0xe0, 0x10, 0x10);
const QColor kLow(0x10, 0x50, 0xc0);
const QColor kLampOff(0xff, 0xff, 0xff);

QString hexDigit(int v) { return v < 0 ? QStringLiteral("-") : QString::number(v, 16).toUpper(); }

} // namespace

ComponentItem::ComponentItem(const Component& c, const SymbolDef* symbol) : m_symbol(symbol)
{
    if (c.id != 0) {
        setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    } else {
        setOpacity(0.55);
        setAcceptedMouseButtons(Qt::NoButton);
    }
    sync(c);
}

SchematicScene* ComponentItem::schematic() const { return qobject_cast<SchematicScene*>(scene()); }

void ComponentItem::sync(const Component& c)
{
    prepareGeometryChange();
    m_c = c;
    setPos(c.pos);
    QTransform t;
    t.rotate(90.0 * c.rotation);
    if (c.mirrored)
        t.scale(-1, 1);
    setTransform(t);
    updateLabelGeometry();
    update();
}

void ComponentItem::updateLabelGeometry()
{
    const QString kind = m_symbol->model.value(u"kind").toString();
    // CircuitMaker shows designators and hides chip part numbers by default
    m_showDesignator = m_c.showDesignator.value_or(!m_symbol->netLabel && kind != u"const") && !m_c.designator.isEmpty();
    m_showLabel = m_c.showLabel.value_or(m_symbol->netLabel || m_symbol->prefix != u"U") && !m_c.label.isEmpty()
               && (m_symbol->netLabel || kind != u"const" || m_c.showLabel.value_or(false));

    // offsets are in scene orientation, relative to pos()
    const QRectF r = transform().mapRect(m_symbol->bounds);
    if (m_symbol->netLabel) {
        m_labelAt = {r.right() + 2, r.center().y()};
    } else {
        m_designatorAt = {r.right() + 3, r.top() + 6};
        m_labelAt = {r.right() + 3, r.top() + (m_showDesignator ? 26 : 6)};
    }
    const QRectF labels(r.right() - 220, r.top() - 30, 440, std::max(r.height(), 50.0) + 60);
    m_bounds = m_symbol->bounds.united(transform().inverted().mapRect(labels)).adjusted(-3, -3, 3, 3);
}

QRectF ComponentItem::boundingRect() const { return m_bounds; }

QPainterPath ComponentItem::shape() const
{
    QPainterPath p;
    p.addRect(m_symbol->bounds);
    return p;
}

QRectF ComponentItem::bodyRect() const
{
    for (const Primitive& g : m_symbol->graphics)
        if (g.type == Primitive::Type::Rect)
            return QRectF(g.v[0], g.v[1], g.v[2], g.v[3]);
    return m_symbol->bounds;
}

int ComponentItem::hexKeyAt(QPointF local) const
{
    const QRectF b = bodyRect();
    if (!b.contains(local))
        return -1;
    const int col = std::clamp(int((local.x() - b.left()) / (b.width() / 4)), 0, 3);
    const int row = std::clamp(int((local.y() - b.top()) / (b.height() / 4)), 0, 3);
    return row * 4 + col;
}

void ComponentItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    const SchematicScene* sc = schematic();
    const bool analog = sc && sc->analogOverlayShown() && m_c.id != 0;
    const bool live = (sc && sc->simulating() && m_c.id != 0) || analog;
    int state = kIdle;
    if (analog) {
        state = sc->analogState(m_c.id);
    } else if (live) {
        state = sc->simState(m_c.id);
    } else if (m_symbol->display == u"logicswitch") {
        state = m_c.props.value(QStringLiteral("state"), m_symbol->defaultProps.value(QStringLiteral("state"))).toInt();
    } else if (m_symbol->display == u"spst") {
        state = m_c.props.value(QStringLiteral("closed"), m_symbol->defaultProps.value(QStringLiteral("closed"))).toInt();
    } else if (m_symbol->display == u"hexkey" || m_symbol->display == u"asciikey") {
        state = m_c.props.value(QStringLiteral("value"), m_symbol->defaultProps.value(QStringLiteral("value"))).toInt();
    }

    const QColor ink = isSelected() ? kSelected : kInk;
    p->setRenderHint(QPainter::Antialiasing);
    paintDynamic(p, state, live, ink);
    SymbolPainter::paint(p, *m_symbol, ink);

    const QTransform toLocal = transform().inverted();
    if (m_showDesignator) {
        if (m_c.designatorAt)
            SymbolPainter::uprightText(p, toLocal.map(*m_c.designatorAt), m_c.designator, 14, 0, ink);
        else
            SymbolPainter::uprightText(p, toLocal.map(m_designatorAt), m_c.designator, 14, -1, ink);
    }
    if (m_showLabel)
    {
        if (m_symbol->id == u"netlabel") {
            // bus entry / net label: name above the wire, on the wire's side
            const QString side = m_c.props.value(QStringLiteral("side"), QStringLiteral("right")).toString();
            const int align = side == u"left" ? 1 : -1;
            const QPointF at = side == u"left" ? QPointF(-4, -10) : side == u"right" ? QPointF(4, -10)
                             : side == u"up" ? QPointF(5, -14) : QPointF(5, 14);
            SymbolPainter::uprightText(p, toLocal.map(at), m_c.label, 16, align, ink);
        } else if (m_c.labelAt) {
            SymbolPainter::uprightText(p, toLocal.map(*m_c.labelAt), m_c.label, 14, 0, ink);
        } else {
            SymbolPainter::uprightText(p, toLocal.map(m_labelAt), m_c.label, 14, -1, ink);
        }
    }
}

void ComponentItem::paintDynamic(QPainter* p, int state, bool live, const QColor& ink)
{
    const QString& d = m_symbol->display;
    if (d.isEmpty())
        return;
    p->save();
    p->setPen(Qt::NoPen);
    const QRectF body = bodyRect();

    auto circles = [&]() {
        QVector<const Primitive*> out;
        for (const Primitive& g : m_symbol->graphics)
            if (g.type == Primitive::Type::Circle)
                out << &g;
        return out;
    };
    auto fillFirst = [&](Primitive::Type type, const QColor& color) {
        for (const Primitive& g : m_symbol->graphics) {
            if (g.type != type)
                continue;
            p->setBrush(color);
            if (type == Primitive::Type::Circle)
                p->drawEllipse(QPointF(g.v[0], g.v[1]), g.v[2], g.v[2]);
            else
                p->drawPath(SymbolPainter::parsePath(g.path));
            return true;
        }
        return false;
    };

    if (d == u"lamp") {
        const QColor c = !live || state == kIdle ? kLampOff : state == 1 ? kHigh : state == 0 ? kLampOff : Qt::yellow;
        if (!fillFirst(Primitive::Type::Circle, c))
            fillFirst(Primitive::Type::Path, c);
    } else if (d == u"led") {
        if (live && state == 1)
            fillFirst(Primitive::Type::Path, kHigh);
    } else if (d == u"stoplight") {
        const QColor on[3] = {QColor(0xe0, 0x10, 0x10), QColor(0xf0, 0xc0, 0x00), QColor(0x10, 0xb0, 0x30)};
        const auto cs = circles();
        for (int i = 0; i < std::min<qsizetype>(3, cs.size()); ++i) {
            const bool lit = live && state > 0 && (state >> i & 1);
            p->setBrush(lit ? on[i] : on[i].lighter(190));
            p->drawEllipse(QPointF(cs[i]->v[0], cs[i]->v[1]), cs[i]->v[2], cs[i]->v[2]);
        }
    } else if (d == u"hex") {
        // red seven-segment digit; segments a..g = bits 0..6
        static constexpr int glyph[16] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07,
                                          0x7f, 0x6f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71};
        const QRectF screen = body.adjusted(10, 10, -10, -10);
        p->setBrush(Qt::white);
        p->drawRect(screen);
        const int lit = state == kIdle || state < 0 ? 0 : glyph[state & 15];
        const double w = screen.width() * 0.32, h = screen.height() * 0.38;
        const QPointF c = screen.center();
        const QLineF seg[7] = {
            {c + QPointF(-w, -2 * h), c + QPointF(w, -2 * h) * 1}, {c + QPointF(w, -2 * h), c + QPointF(w, 0)},
            {c + QPointF(w, 0), c + QPointF(w, 2 * h)},           {c + QPointF(-w, 2 * h), c + QPointF(w, 2 * h)},
            {c + QPointF(-w, 0), c + QPointF(-w, 2 * h)},         {c + QPointF(-w, -2 * h), c + QPointF(-w, 0)},
            {c + QPointF(-w, 0), c + QPointF(w, 0)},
        };
        const double scaleH = h / 2;
        Q_UNUSED(scaleH);
        for (int i = 0; i < 7; ++i) {
            const bool on = lit >> i & 1;
            p->setPen(QPen(on ? QColor(0xff, 0x10, 0x10) : QColor(0xf0, 0xb0, 0xb0), on ? 6 : 2, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(QLineF(seg[i].p1() * 1, seg[i].p2()));
        }
    } else if (d == u"ascii") {
        const QRectF screen = body.adjusted(8, 8, -8, -8);
        p->setBrush(QColor(0x20, 0x20, 0x20));
        p->drawRect(screen);
        if (state != kIdle) {
            const QString text = d == u"hex" ? hexDigit(state)
                               : state < 0 ? QStringLiteral("-")
                               : state >= 32 && state < 127 ? QString(QChar(state)) : QStringLiteral("\u00b7");
            SymbolPainter::uprightText(p, screen.center(), text, screen.height() * 0.6, 0, QColor(0xff, 0x30, 0x20));
        }
    } else if (d == u"seg7") {
        // a..g on a digit 50 wide, 110 high, centred in the body; dp bottom right
        const QPointF c = body.center() + QPointF(-5, -5);
        const double w = 25, h = 55;
        const QLineF seg[7] = {
            {c + QPointF(-w + 5, -h), c + QPointF(w - 5, -h)}, {c + QPointF(w, -h + 5), c + QPointF(w, -5)},
            {c + QPointF(w, 5), c + QPointF(w, h - 5)},        {c + QPointF(-w + 5, h), c + QPointF(w - 5, h)},
            {c + QPointF(-w, 5), c + QPointF(-w, h - 5)},      {c + QPointF(-w, -h + 5), c + QPointF(-w, -5)},
            {c + QPointF(-w + 5, 0), c + QPointF(w - 5, 0)},
        };
        const int lit = live && state > 0 ? state : 0;
        for (int i = 0; i < 7; ++i) {
            QPen pen(lit >> i & 1 ? kHigh : QColor(0xe8, 0xe0, 0xe0), 8, Qt::SolidLine, Qt::RoundCap);
            p->setPen(pen);
            p->drawLine(seg[i]);
        }
        p->setPen(Qt::NoPen);
        p->setBrush(lit >> 7 & 1 ? kHigh : QColor(0xe8, 0xe0, 0xe0));
        p->drawEllipse(c + QPointF(w + 12, h), 5, 5);
    } else if (d == u"logicswitch") {
        // lever from the output pivot to the 1 (upper) or 0 (lower) contact
        const bool on = state == 1;
        p->setPen(QPen(on ? kHigh : ink, 2, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(QPointF(20, 0), on ? QPointF(-4, -11) : QPointF(-6, 9));
        p->setPen(Qt::NoPen);
        p->setBrush(on ? kHigh : ink);
        p->drawEllipse(QPointF(20, 0), 3, 3);
        if (live)
            SymbolPainter::uprightText(p, QPointF(-14, on ? -22 : 22), on ? QStringLiteral("1") : QStringLiteral("0"), 9, 0,
                                       on ? kHigh : kLow);
    } else if (d == u"pulser") {
        if (live) {
            p->setBrush(state == 1 ? kHigh : kLow);
            p->drawEllipse(body.topRight() + QPointF(-9, 9), 5, 5);
        }
    } else if (d == u"spst" || d == u"button") {
        const auto cs = circles();
        if (cs.size() >= 2) {
            const QPointF a(cs[0]->v[0], cs[0]->v[1]), b(cs[1]->v[0], cs[1]->v[1]);
            const QString type = m_symbol->model.value(u"type").toString();
            if (d == u"spst") {
                p->setPen(QPen(ink, 2));
                p->drawLine(a, state == 1 ? b : b + QPointF(-5, -22));
            } else if (live) {
                // highlight the conducting contact pair
                const bool pressed = state == 1;
                bool conducting = type == u"nc" ? !pressed : pressed;
                QPointF from = a, to = b;
                if (type == u"spdt" && cs.size() >= 3) {
                    conducting = true;
                    to = pressed ? QPointF(cs[2]->v[0], cs[2]->v[1]) : b;
                }
                if (conducting) {
                    p->setPen(QPen(kHigh, 3, Qt::SolidLine, Qt::RoundCap));
                    p->drawLine(from, to);
                }
            }
        }
    } else if (d == u"hexkey") {
        const double cw = body.width() / 4, ch = body.height() / 4;
        for (int k = 0; k < 16; ++k) {
            const QRectF cell(body.left() + (k % 4) * cw, body.top() + (k / 4) * ch, cw, ch);
            p->setPen(QPen(ink, 0.8));
            p->setBrush(state == k ? QColor(0xff, 0xd0, 0x40) : QColor(0xf4, 0xf4, 0xf4));
            p->drawRect(cell.adjusted(3, 3, -3, -3));
            SymbolPainter::uprightText(p, cell.center(), hexDigit(k), 11, 0, ink);
        }
    } else if (d == u"meter") {
        const QRectF screen = body.adjusted(10, 10, -10, -30);
        p->setPen(QPen(ink, 1));
        p->setBrush(QColor(0xd8, 0xe8, 0xd0));
        p->drawRect(screen);
        const SchematicScene* sc = schematic();
        QString text = sc ? sc->meterText(m_c.id) : QString();
        if (text.isEmpty())
            text = m_c.props.value(QStringLiteral("mode"), QStringLiteral("V")).toString() == u"A" ? QStringLiteral("— A")
                                                                                                  : QStringLiteral("— V");
        SymbolPainter::uprightText(p, screen.center(), text, 14, 0, QColor(0x10, 0x30, 0x10));
    } else if (d == u"bulb") {
        if (live && state > 0) {
            p->setBrush(QColor(0xff, 0xe0, 0x40));
            fillFirst(Primitive::Type::Circle, QColor(0xff, 0xe0, 0x40));
        }
    } else if (d == u"asciikey") {
        const QString text = state >= 32 && state < 127 ? QString(QChar(state)) : QStringLiteral("\u00b7");
        SymbolPainter::uprightText(p, body.center() + QPointF(0, -10), text, 36, 0, ink);
        SymbolPainter::uprightText(p, body.center() + QPointF(0, 32), QStringLiteral("0x%1").arg(state, 2, 16, QChar(u'0')),
                                   8, 0, ink);
    }
    p->restore();
}

QVariant ComponentItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && scene()) {
        const QPointF p = value.toPointF();
        return QPointF(std::round(p.x() / SchematicScene::kGrid) * SchematicScene::kGrid,
                       std::round(p.y() / SchematicScene::kGrid) * SchematicScene::kGrid);
    }
    return QGraphicsItem::itemChange(change, value);
}

} // namespace cm
