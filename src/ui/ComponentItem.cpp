#include "ui/ComponentItem.h"

#include "library/LibraryRegistry.h"
#include "ui/Theme.h"

#include "library/SymbolDef.h"
#include "ui/SchematicScene.h"
#include "ui/SymbolPainter.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QRadialGradient>
#include <QStyleOptionGraphicsItem>

#include <climits>
#include <cmath>

namespace cm {

namespace {

constexpr int kIdle = INT_MIN;     // not simulating: displays stay blank

const QColor kLampOff(0xff, 0xff, 0xff);

QString hexDigit(int v) { return v < 0 ? QStringLiteral("-") : QString::number(v, 16).toUpper(); }

QColor ledColor(const QString& name)
{
    if (name == u"green") return {0x10, 0xc0, 0x30};
    if (name == u"yellow") return {0xf5, 0xd0, 0x00};
    if (name == u"blue") return {0x20, 0x70, 0xff};
    if (name == u"orange") return {0xff, 0x80, 0x00};
    if (name == u"white") return {0xd8, 0xe8, 0xff};
    return {0xff, 0x20, 0x20};
}

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
    QRectF labels(r.right() - 220, r.top() - 30, 440, std::max(r.height(), 50.0) + 60);
    // texts moved elsewhere
    for (const std::optional<QPointF>& at : {m_c.designatorAt, m_c.labelAt})
        if (at) labels |= QRectF(*at - QPointF(150, 30), QSizeF(300, 60));
    m_bounds = m_symbol->bounds.united(transform().inverted().mapRect(labels)).adjusted(-3, -3, 3, 3);
}

QRectF ComponentItem::boundingRect() const { return m_bounds; }

QPointF ComponentItem::textAnchor(int which) const
{
    if (which == 0 && m_c.designatorAt) return *m_c.designatorAt;
    if (which == 1 && m_c.labelAt) return *m_c.labelAt;
    // the default places are left-aligned: their centre
    const QString text = which == 0 ? shownDesignator(*m_symbol, m_c) : m_c.label;
    const double w = QFontMetricsF(SymbolPainter::schematicFont(14)).horizontalAdvance(text);
    return (which == 0 ? m_designatorAt : m_labelAt) + QPointF(w / 2, 0);
}

QRectF ComponentItem::textRect(int which) const
{
    const QString text = which == 0 ? shownDesignator(*m_symbol, m_c) : m_c.label;
    const QFontMetricsF fm(SymbolPainter::schematicFont(14));
    const double w = fm.horizontalAdvance(text);
    const QPointF a = textAnchor(which);
    return QRectF(a.x() - w / 2, a.y() - fm.ascent(), w, fm.ascent() + fm.descent()).adjusted(-2, -2, 2, 2);
}

int ComponentItem::textAt(QPointF scenePos) const
{
    // net labels and ports draw their names in fixed places
    if (m_symbol->netLabel) return -1;
    const QPointF rel = scenePos - pos();
    if (m_showDesignator && textRect(0).contains(rel)) return 0;
    if (m_showLabel && textRect(1).contains(rel)) return 1;
    return -1;
}

void ComponentItem::setTextPreview(int which, std::optional<QPointF> anchor)
{
    prepareGeometryChange();
    m_previewText = anchor ? which : -1;
    if (anchor) {
        m_previewAt = *anchor;
        // keep the dragged text inside the item's area
        const QRectF r = transform().inverted().mapRect(QRectF(*anchor - QPointF(150, 30), QSizeF(300, 60)));
        m_bounds = m_bounds.united(r);
    }
    update();
}

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
    } else if (m_symbol->display == u"spst" || m_symbol->display == u"window") {
        state = m_c.props.value(QStringLiteral("closed"), m_symbol->defaultProps.value(QStringLiteral("closed"))).toInt();
    } else if (m_symbol->display == u"hexkey" || m_symbol->display == u"asciikey") {
        state = m_c.props.value(QStringLiteral("value"), m_symbol->defaultProps.value(QStringLiteral("value"))).toInt();
    }

    const QColor ink = isSelected() ? theme().selection : theme().device;
    p->setRenderHint(QPainter::Antialiasing);
    paintDynamic(p, state, live, ink);
    // the hex/ASCII key that has the keyboard
    if (live && sc && sc->keyFocus() == m_c.id) {
        p->save();
        p->setPen(QPen(theme().selection, 1.5, Qt::DashLine));
        p->setBrush(Qt::NoBrush);
        p->drawRect(bodyRect().adjusted(-4, -4, 4, 4));
        p->restore();
    }
    // push buttons: the plunger travels while held
    QPointF actuator;
    if (m_symbol->display == u"button" && live && state == 1)
        actuator = QPointF(0, m_symbol->defaultProps.value(QStringLiteral("travel"), 7).toDouble());
    SymbolPainter::paint(p, *m_symbol, ink, actuator);

    const QTransform toLocal = transform().inverted();
    if (m_showDesignator) {
        if (m_previewText == 0)
            SymbolPainter::uprightText(p, toLocal.map(m_previewAt), shownDesignator(*m_symbol, m_c), 14, 0, ink);
        else if (m_c.designatorAt)
            SymbolPainter::uprightText(p, toLocal.map(*m_c.designatorAt), shownDesignator(*m_symbol, m_c), 14, 0, ink);
        else
            SymbolPainter::uprightText(p, toLocal.map(m_designatorAt), shownDesignator(*m_symbol, m_c), 14, -1, ink);
    }
    if (m_showLabel)
    {
        if (m_symbol->display == u"port") {
            // port name inside the arrow
            const double cx = m_symbol->id == u"port_in" ? -32 : 28;
            SymbolPainter::uprightText(p, QPointF(cx, 0), m_c.label, 11, 0, ink);
        } else if (m_symbol->id == u"netlabel") {
            // bus entry / net label: name above the wire, on the wire's side
            const QString side = m_c.props.value(QStringLiteral("side"), QStringLiteral("right")).toString();
            const int align = side == u"left" ? 1 : -1;
            const QPointF at = side == u"left" ? QPointF(-4, -10) : side == u"right" ? QPointF(4, -10)
                             : side == u"up" ? QPointF(5, -14) : QPointF(5, 14);
            SymbolPainter::uprightText(p, toLocal.map(at), m_c.label, 16, align, ink);
        } else if (m_previewText == 1) {
            SymbolPainter::uprightText(p, toLocal.map(m_previewAt), m_c.label, 14, 0, ink);
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
        const QString name = m_c.props.value(QStringLiteral("color"), m_symbol->defaultProps.value(QStringLiteral("color"),
                                                                                                    QStringLiteral("red"))).toString();
        const QColor color = ledColor(name);
        const bool on = live && state == 1;
        const bool unknown = live && state < 0 && state != kIdle;
        const auto cs = circles();
        if (cs.size() >= 2) {
            // bezel ring, then the lamp: pale when off, bright with a glow and highlight when on
            const QPointF c(cs[0]->v[0], cs[0]->v[1]);
            const double outer = cs[0]->v[2], inner = cs[1]->v[2];
            if (on) {
                QRadialGradient glow(c, outer * 1.6);
                QColor halo = color;
                halo.setAlpha(140);
                glow.setColorAt(0.5, halo);
                halo.setAlpha(0);
                glow.setColorAt(1, halo);
                p->setBrush(glow);
                p->drawEllipse(c, outer * 1.6, outer * 1.6);
            }
            p->setBrush(QColor(0xe4, 0xe4, 0xe4));
            p->drawEllipse(c, outer, outer);
            QRadialGradient lamp(c - QPointF(inner * 0.35, inner * 0.35), inner * 1.3);
            const QColor base = on ? color : color.lighter(name == u"white" ? 104 : 185);
            lamp.setColorAt(0, on ? base.lighter(150) : base.lighter(110));
            lamp.setColorAt(1, on ? base : base.darker(105));
            p->setBrush(lamp);
            p->drawEllipse(c, inner, inner);
            if (on) {
                p->setBrush(QColor(255, 255, 255, 170));
                p->drawEllipse(c - QPointF(inner * 0.4, inner * 0.4), inner * 0.28, inner * 0.2);
            }
            if (unknown)
                SymbolPainter::uprightText(p, c, QStringLiteral("X"), 12, 0, QColor(0x90, 0x60, 0x00));
        } else {
            const QColor c = !live || state == kIdle ? kLampOff : on ? color : state == 0 ? kLampOff : Qt::yellow;
            if (!fillFirst(Primitive::Type::Circle, c))
                fillFirst(Primitive::Type::Path, c);
        }
    } else if (d == u"led") {
        const QString name = m_c.props.value(QStringLiteral("color"), m_symbol->defaultProps.value(QStringLiteral("color"))).toString();
        const QColor color = ledColor(name);
        const bool on = live && state == 1;
        if (on) {
            // glow around the lit LED
            QRadialGradient glow(QPointF(0, 0), 26);
            QColor halo = color;
            halo.setAlpha(150);
            glow.setColorAt(0, halo);
            halo.setAlpha(0);
            glow.setColorAt(1, halo);
            p->setBrush(glow);
            p->drawEllipse(QPointF(0, 0), 26, 26);
        }
        QColor body = on ? color : color.lighter(name == u"white" ? 100 : 175);
        if (!on) body.setAlpha(name == u"white" ? 60 : 255);
        fillFirst(Primitive::Type::Path, body);
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
        // digit area above the 8-4-2-1 pin captions
        const QRectF screen = body.adjusted(14, 10, -14, -24);
        const int lit = state == kIdle || state < 0 ? 0 : glyph[state & 15];
        const double w = screen.width() * 0.32, h = screen.height() * 0.21;
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
            QPen pen(lit >> i & 1 ? theme().high : QColor(0xe8, 0xe0, 0xe0), 8, Qt::SolidLine, Qt::RoundCap);
            p->setPen(pen);
            p->drawLine(seg[i]);
        }
        p->setPen(Qt::NoPen);
        p->setBrush(lit >> 7 & 1 ? theme().high : QColor(0xe8, 0xe0, 0xe0));
        p->drawEllipse(c + QPointF(w + 12, h), 5, 5);
    } else if (d == u"logicswitch") {
        // lever from the output pivot to the 1 (upper) or 0 (lower) contact
        const bool on = state == 1;
        p->setPen(QPen(on ? theme().high : ink, 2, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(QPointF(20, 0), on ? QPointF(-4, -11) : QPointF(-6, 9));
        p->setPen(Qt::NoPen);
        p->setBrush(on ? theme().high : ink);
        p->drawEllipse(QPointF(20, 0), 3, 3);
        if (live)
            SymbolPainter::uprightText(p, QPointF(-14, on ? -22 : 22), on ? QStringLiteral("1") : QStringLiteral("0"), 9, 0,
                                       on ? theme().high : theme().low);
    } else if (d == u"pulser") {
        if (live) {
            p->setBrush(state == 1 ? theme().high : theme().low);
            p->drawEllipse(body.topRight() + QPointF(-9, 9), 5, 5);
        }
    } else if (d == u"dataseq") {
        // present address while simulating
        if (live && state >= 0)
            SymbolPainter::uprightText(p, QPointF(0, 60), QStringLiteral("%1").arg(state, 2, 16, QChar(u'0')).toUpper(), 9, 0, theme().high);
    } else if (d == u"spst" || d == u"button") {
        const auto cs = circles();
        const QString type = m_symbol->model.value(u"type").toString();
        if (d == u"spst" && type != u"spdt" && cs.size() >= 2) {
            const QPointF a(cs[0]->v[0], cs[0]->v[1]), b(cs[1]->v[0], cs[1]->v[1]);
            p->setPen(QPen(live && state == 1 ? theme().high : ink, 2, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(a, state == 1 ? b : a + (b - a) * 0.85 + QPointF(0, -(b - a).manhattanLength() * 0.45));
        } else if (type == u"spdt" && cs.size() >= 3) {
            // lever from the common contact to NC at rest, to NO while held
            // (a toggle switch keeps its position)
            const bool thrown = state == 1 && (live || d == u"spst");
            const QPointF com(cs[0]->v[0], cs[0]->v[1]);
            const QPointF to = thrown ? QPointF(cs[2]->v[0], cs[2]->v[1]) : QPointF(cs[1]->v[0], cs[1]->v[1]);
            p->setPen(QPen(live && thrown ? theme().high : ink, 2, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(com, com + (to - com) * 0.88);
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
    } else if (d == u"window") {
        // a sash window: the lower sash slides up over the upper one when open
        const bool open = state == 1;
        auto sash = [&](double top) {
            const QRectF r(-80, top, 140, 95);
            p->setPen(QPen(ink, 2.5));
            p->setBrush(Qt::NoBrush);
            p->drawRect(r);
            p->setPen(QPen(ink, 1.5));
            p->drawRect(r.adjusted(8, 8, -8, -8));
            p->drawLine(QPointF(r.center().x(), r.top() + 8), QPointF(r.center().x(), r.bottom() - 8));
            p->drawLine(QPointF(r.left() + 8, r.center().y()), QPointF(r.right() - 8, r.center().y()));
        };
        sash(-100);
        if (!open) {
            p->setPen(QPen(ink, 1.5));
            p->drawLine(QPointF(-64, -70), QPointF(-50, -84));         // the glass shines while shut
            p->drawLine(QPointF(-58, -66), QPointF(-44, -80));
        }
        sash(open ? -90 : 0);
        if (live && open) {
            p->setPen(QPen(theme().high, 2));
            p->drawRect(QRectF(-80, 5, 140, 90));
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
