#include "ui/ComponentItem.h"

#include "core/Generator.h"
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

// designators and labels: CircuitMaker's Courier at its size (a character 7
// of its units wide, 23 of ours)
constexpr double kTextSize = 28;

namespace {

constexpr int kIdle = INT_MIN;     // not simulating: displays stay blank

const QColor kLampOff(0xff, 0xff, 0xff);

QString hexDigit(int v) { return v < 0 ? QStringLiteral("-") : QString::number(v, 16).toUpper(); }

QColor ledColor(const QString& name)
{
    if (name == u"green") return {0x10, 0xc0, 0x30};
    if (name == u"yellow") return {0xf5, 0xd0, 0x00};
    if (name == u"blue") return {0x20, 0x70, 0xff};
    if (name == u"cyan") return {0x00, 0xd0, 0xe0};
    if (name == u"magenta") return {0xe0, 0x30, 0xe0};
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
    m_showLabel = m_c.showLabel.value_or((m_symbol->netLabel || m_symbol->prefix != u"U") && !m_symbol->labelInside) && !m_c.label.isEmpty()
               && (m_symbol->netLabel || kind != u"const" || m_c.showLabel.value_or(false));

    // offsets are in scene orientation, relative to pos()
    const QRectF r = transform().mapRect(m_symbol->bounds);
    if (m_symbol->netLabel) {
        m_labelAt = {r.right() + 2, r.center().y()};
    } else {
        m_designatorAt = {r.right() + 4, r.top() + 12};
        m_labelAt = {r.right() + 4, r.top() + (m_showDesignator ? 50 : 12)};
    }
    QRectF labels(r.right() - 400, r.top() - 50, 800, std::max(r.height(), 90.0) + 100);
    // texts moved elsewhere
    for (const std::optional<QPointF>& at : {m_c.designatorAt, m_c.labelAt})
        if (at) labels |= QRectF(*at - QPointF(300, 50), QSizeF(600, 100));
    if (const QStringList tag = m_c.props.value(QStringLiteral("cmTagAt")).toString().split(u' ', Qt::SkipEmptyParts); tag.size() == 2)
        labels |= QRectF(QPointF(tag[0].toDouble(), tag[1].toDouble()) - QPointF(300, 50), QSizeF(600, 100));
    m_bounds = m_symbol->bounds.united(transform().inverted().mapRect(labels)).adjusted(-3, -3, 3, 3);
    // a Car drives out to the right of its drawing
    if (m_symbol->display == u"car")
        m_bounds |= QRectF(m_symbol->bounds.left(), -40, m_symbol->bounds.width() + carTravel() + 130, 70);
}

QRectF ComponentItem::boundingRect() const { return m_bounds; }

QPointF ComponentItem::textAnchor(int which) const
{
    if (which == 0 && m_c.designatorAt) return *m_c.designatorAt;
    if (which == 1 && m_c.labelAt) return *m_c.labelAt;
    // the default places are left-aligned: their centre
    const QString text = which == 0 ? shownDesignator(*m_symbol, m_c) : m_c.label;
    const double w = QFontMetricsF(SymbolPainter::schematicFont(kTextSize)).horizontalAdvance(text);
    return (which == 0 ? m_designatorAt : m_labelAt) + QPointF(w / 2, 0);
}

QRectF ComponentItem::textRect(int which) const
{
    const QString text = which == 0 ? shownDesignator(*m_symbol, m_c) : m_c.label;
    const QFontMetricsF fm(SymbolPainter::schematicFont(kTextSize));
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

// CircuitMaker's 4x4 key matrix has 0-3 on the bottom row, C-F on top
int ComponentItem::keyRow(int row) const
{
    return m_symbol->model.value(u"kind").toString() == u"keymatrix" ? 3 - row : row;
}

double ComponentItem::carTravel() const
{
    const int distance = m_c.props.value(QStringLiteral("distance"), m_symbol->defaultProps.value(QStringLiteral("distance"), 400)).toInt();
    return std::clamp(distance, 0, 1000) * 10.0 / 3.0;
}

bool ComponentItem::onCarReset(QPointF local) const
{
    // the second box of the Car's drawing is its Reset button
    int rects = 0;
    for (const Primitive& g : m_symbol->graphics)
        if (g.type == Primitive::Type::Rect && ++rects == 2)
            return QRectF(g.v[0], g.v[1], g.v[2], g.v[3]).contains(local);
    return false;
}

bool ComponentItem::singleKey() const
{
    return m_symbol->display == u"hexkey" && m_symbol->model.value(u"kind").toString() != u"keymatrix";
}

// CircuitMaker's key face, above the pin numbers
QRectF ComponentItem::keyFace() const
{
    const QRectF b = bodyRect();
    if (m_symbol->display == u"asciikey")
        return QRectF(b.center().x() - 50, b.top() + 6, 100, 48);
    return QRectF(b.left() + 8, b.top() + 8, b.width() - 16, b.width() - 16);
}

int ComponentItem::hexKeyAt(QPointF local) const
{
    const QRectF b = bodyRect();
    if (!b.contains(local))
        return -1;
    // the Hex Key counts up on each click, as CircuitMaker's does, and the
    // ASCII Key steps through the printable characters
    if (singleKey() || m_symbol->display == u"asciikey") {
        if (!keyFace().contains(local))
            return -1;
        const SchematicScene* sc = schematic();
        const int now = sc && sc->simulating() ? sc->simState(m_c.id)
                                               : m_c.props.value(QStringLiteral("value"), m_symbol->defaultProps.value(QStringLiteral("value"))).toInt();
        if (singleKey())
            return (std::max(now, 0) + 1) % 16;
        return now >= 32 && now < 126 ? now + 1 : 32;
    }
    const int col = std::clamp(int((local.x() - b.left()) / (b.width() / 4)), 0, 3);
    const int row = std::clamp(int((local.y() - b.top()) / (b.height() / 4)), 0, 3);
    return keyRow(row) * 4 + col;
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
    } else if ((m_symbol->display == u"hexkey" && m_symbol->model.value(u"kind").toString() != u"keymatrix")
               || m_symbol->display == u"asciikey") {
        // a key matrix holds no key until one is pressed while simulating
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
    // the label inside the body (CircuitMaker's flag 0x80), unless it is shown outside
    SymbolPainter::paint(p, *m_symbol, ink, actuator, m_showLabel ? nullptr : &m_c.label);

    const QTransform toLocal = transform().inverted();
    if (m_showDesignator) {
        if (m_previewText == 0)
            SymbolPainter::uprightText(p, toLocal.map(m_previewAt), shownDesignator(*m_symbol, m_c), kTextSize, 0, ink);
        else if (m_c.designatorAt)
            SymbolPainter::uprightText(p, toLocal.map(*m_c.designatorAt), shownDesignator(*m_symbol, m_c), kTextSize, 0, ink);
        else
            SymbolPainter::uprightText(p, toLocal.map(m_designatorAt), shownDesignator(*m_symbol, m_c), kTextSize, -1, ink);
    }
    // CircuitMaker's third label where it showed it (flag 0x400): the coil
    // a contact or solenoid follows
    if (const QStringList at = m_c.props.value(QStringLiteral("cmTagAt")).toString().split(u' ', Qt::SkipEmptyParts); at.size() == 2) {
        QString tag = m_c.props.value(QStringLiteral("relay")).toString();
        if (tag.isEmpty()) tag = m_c.props.value(QStringLiteral("cmTag")).toString().section(u' ', 5);
        if (!tag.isEmpty())
            SymbolPainter::uprightText(p, toLocal.map(QPointF(at[0].toDouble(), at[1].toDouble())), tag, kTextSize, 0, ink);
    }
    if (m_symbol->display == u"port" && !m_c.label.isEmpty()) {
        // port name inside the arrow, always, as CircuitMaker shows it
        const double cx = m_symbol->id == u"port_in" ? -118 : 108;
        SymbolPainter::uprightText(p, QPointF(cx, 0), m_c.label, kTextSize * 0.8, 0, ink);
    } else if (m_showLabel)
    {
        if (m_symbol->id == u"netlabel") {
            // bus entry / net label: name above the wire, on the wire's side
            const QString side = m_c.props.value(QStringLiteral("side"), QStringLiteral("right")).toString();
            const int align = side == u"left" ? 1 : -1;
            if ((side == u"up" || side == u"down") && m_c.label.size() > 1) {
                // on an upright entry CircuitMaker stacks the letters, one a row
                const double step = 26, first = side == u"up" ? -16 - step * (m_c.label.size() - 1) : 20;
                for (int i = 0; i < m_c.label.size(); ++i)
                    SymbolPainter::uprightText(p, toLocal.map(QPointF(13, first + i * step)), m_c.label.mid(i, 1), kTextSize, 0, ink);
            } else {
                const QPointF at = side == u"left" ? QPointF(-4, -16) : side == u"right" ? QPointF(4, -16)
                                 : side == u"up" ? QPointF(5, -20) : QPointF(5, 20);
                SymbolPainter::uprightText(p, toLocal.map(at), m_c.label, kTextSize, align, ink);
            }
        } else if (m_previewText == 1) {
            SymbolPainter::uprightText(p, toLocal.map(m_previewAt), m_c.label, kTextSize, 0, ink);
        } else if (m_c.labelAt) {
            SymbolPainter::uprightText(p, toLocal.map(*m_c.labelAt), m_c.label, kTextSize, 0, ink);
        } else {
            SymbolPainter::uprightText(p, toLocal.map(m_labelAt), m_c.label, kTextSize, -1, ink);
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
        const Primitive* dome = nullptr;
        for (const Primitive& g : m_symbol->graphics)
            if (g.type == Primitive::Type::Path && !dome) dome = &g;
        if (m_symbol->id == u"logicdisplay" && dome) {
            // CircuitMaker's Logic Display: the dome outlined in its colour,
            // filled with it when lit
            const QPainterPath path = SymbolPainter::parsePath(dome->path);
            p->setPen(QPen(unknown ? QColor(0xc0, 0x90, 0x00) : color.darker(name == u"white" ? 130 : 100), 2));
            p->setBrush(on ? QBrush(color) : QBrush(Qt::white));
            p->drawPath(path);
            p->setPen(Qt::NoPen);
            if (unknown)
                SymbolPainter::uprightText(p, QPointF(0, -16), QStringLiteral("X"), 12, 0, QColor(0x90, 0x60, 0x00));
        } else if (cs.size() >= 2) {
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
            // as in CircuitMaker: a coloured ring when off, filled when lit
            const bool lit = live && state > 0 && (state >> i & 1);
            p->setPen(QPen(on[i], 3));
            p->setBrush(lit ? on[i] : QColor(Qt::white));
            p->drawEllipse(QPointF(cs[i]->v[0], cs[i]->v[1]), cs[i]->v[2] - 1.5, cs[i]->v[2] - 1.5);
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
    } else if (d == u"ascii" && m_symbol->model.value(u"kind").toString() == u"asciiterm") {
        // the terminal: 4 lines of 16, as saved or as the simulation wrote them
        const QRectF screen = body.adjusted(14, 14, -14, -14);
        p->setPen(QPen(ink, 1));
        p->setBrush(QColor(0x30, 0xe0, 0xf0));
        p->drawRoundedRect(screen, 4, 4);
        QString text;
        int cursor = -1;
        const SchematicScene* sc = schematic();
        if (const QVector<int> mem = live && sc ? sc->simMemory(m_c.id) : QVector<int>{}; mem.size() == 65) {
            for (int i = 0; i < 64; ++i) text += QChar(mem[i]);
            cursor = mem[64];
        } else {
            text = m_c.props.value(QStringLiteral("text")).toString();
        }
        text = text.leftJustified(64, u' ', true);
        QFont f(QStringLiteral("Courier New"));
        f.setStyleHint(QFont::Monospace);
        f.setPixelSize(int(screen.height() / 4 * 0.75));
        p->setFont(f);
        p->setPen(QColor(0x10, 0x10, 0x10));
        const double lh = screen.height() / 4, cw = (screen.width() - 12) / 16;
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 16; ++col) {
                const QRectF cell(screen.left() + 6 + col * cw, screen.top() + row * lh, cw, lh);
                p->drawText(cell, Qt::AlignCenter, text.mid(row * 16 + col, 1));
                if (row * 16 + col == cursor)
                    p->drawLine(cell.bottomLeft() - QPointF(-1, 4), cell.bottomRight() - QPointF(1, 4));
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
    } else if (d == u"siggen") {
        // as CircuitMaker draws a generator: its waveform, its frequency below
        QVariantMap props = m_symbol->defaultProps;
        for (auto it = m_c.props.cbegin(); it != m_c.props.cend(); ++it) props.insert(it.key(), it.value());
        const QString wave = props.value(QStringLiteral("wave"), QStringLiteral("sine")).toString();
        auto num = [&](const char* key, const char* fallback) { return generator::prop(props, key, fallback); };
        QVector<QPointF> tv;                        // (time, value)
        if (wave == u"pulse") {
            const double v1 = num("v1", "0"), v2 = num("v2", "5"), delay = std::max(num("delay", "0"), 0.0);
            const double rise = std::max(num("rise", "1n"), 0.0), fall = std::max(num("fall", "1n"), 0.0);
            const double width = std::max(num("width", "0.5m"), 0.0), period = std::max(num("period", "1m"), rise + width + fall);
            tv << QPointF(0, v1);
            // one period, as CircuitMaker shows it
            tv << QPointF(delay, v1) << QPointF(delay + rise, v2) << QPointF(delay + rise + width, v2)
               << QPointF(delay + rise + width + fall, v1) << QPointF(delay + period, v1);
        } else if (wave == u"pwl") {
            QVector<double> pts;
            static const QRegularExpression sep(QStringLiteral("[\\s,;]+"));
            for (const QString& t : props.value(QStringLiteral("pwl")).toString().split(sep, Qt::SkipEmptyParts)) pts << generator::spiceNumber(t);
            for (int k = 0; k + 1 < pts.size(); k += 2) tv << QPointF(pts[k], pts[k + 1]);
        } else if (wave == u"exp") {
            const double v1 = num("v1", "0"), v2 = num("v2", "1"), rd = num("rd", "0"), rt = std::max(num("rt", "1"), 1e-15);
            const double fd = std::max(num("fd", "1"), rd), ft = std::max(num("ft", "1"), 1e-15), end = fd + 5 * ft;
            for (int i = 0; i <= 40; ++i) {
                const double t = end * i / 40;
                double v = v1;
                if (t > rd) v += (v2 - v1) * (1 - std::exp(-(t - rd) / rt));
                if (t > fd) v += (v1 - v2) * (1 - std::exp(-(t - fd) / ft));
                tv << QPointF(t, v);
            }
        } else {
            // a sine, one cycle as CircuitMaker shows it; FM more of them
            const int cycles = wave == u"fm" ? 3 : 1;
            for (int i = 0; i <= 16 * cycles; ++i) tv << QPointF(i, std::sin(i * M_PI / 8));
        }
        if (tv.size() >= 2) {
            double t0 = tv.first().x(), t1 = tv.first().x(), lo = tv.first().y(), hi = tv.first().y();
            for (QPointF q : std::as_const(tv)) t0 = std::min(t0, q.x()), t1 = std::max(t1, q.x()), lo = std::min(lo, q.y()), hi = std::max(hi, q.y());
            const QRectF box(body.left() + 12, body.top() + 12, body.width() - 24, body.height() * 0.45);
            QPolygonF line;
            for (QPointF q : std::as_const(tv))
                line << QPointF(box.left() + (t1 > t0 ? (q.x() - t0) / (t1 - t0) : 0.5) * box.width(),
                                box.bottom() - (hi > lo ? (q.y() - lo) / (hi - lo) : 0.5) * box.height());
            p->setPen(QPen(ink, 1.5));
            p->setBrush(Qt::NoBrush);
            p->drawPolyline(line);
        }
        SymbolPainter::uprightText(p, QPointF(body.center().x(), body.bottom() - 13), generator::frequencyText(generator::frequency(props)), 11, 0, ink);
    } else if (d == u"seg7") {
        // a..g on a digit 50 wide, 90 high, between the labels; dp bottom right
        const QPointF c = body.center() + QPointF(-5, -2);
        const double w = 25, h = 45;
        const QLineF seg[7] = {
            {c + QPointF(-w + 5, -h), c + QPointF(w - 5, -h)}, {c + QPointF(w, -h + 5), c + QPointF(w, -5)},
            {c + QPointF(w, 5), c + QPointF(w, h - 5)},        {c + QPointF(-w + 5, h), c + QPointF(w - 5, h)},
            {c + QPointF(-w, 5), c + QPointF(-w, h - 5)},      {c + QPointF(-w, -h + 5), c + QPointF(-w, -5)},
            {c + QPointF(-w + 5, 0), c + QPointF(w - 5, 0)},
        };
        // only lit segments show, as in CircuitMaker
        const int lit = live && state > 0 ? state : 0;
        for (int i = 0; i < 7; ++i) {
            if (!(lit >> i & 1)) continue;
            p->setPen(QPen(theme().high, 8, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(seg[i]);
        }
        p->setPen(Qt::NoPen);
        if (lit >> 7 & 1) {
            p->setBrush(theme().high);
            p->drawEllipse(c + QPointF(w + 12, h), 5, 5);
        }
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
    } else if (singleKey() || d == u"asciikey") {
        // a bevelled key showing its number (or character)
        const QRectF face = keyFace(), top = face.adjusted(face.height() / 5, face.height() / 5, -face.height() / 5, -face.height() / 5);
        p->setPen(QPen(ink, 1));
        p->setBrush(QColor(0xf4, 0xf4, 0xf4));
        p->drawRect(face);
        p->setBrush(Qt::NoBrush);
        p->drawRect(top);
        p->drawLine(face.topLeft(), top.topLeft());
        p->drawLine(face.topRight(), top.topRight());
        p->drawLine(face.bottomLeft(), top.bottomLeft());
        p->drawLine(face.bottomRight(), top.bottomRight());
        const QString shown = singleKey() ? hexDigit(std::clamp(state, 0, 15))
                                          : state >= 32 && state < 127 ? QString(QChar(state)) : QStringLiteral("\u00b7");
        SymbolPainter::uprightText(p, top.center(), shown, 16, 0, ink);
    } else if (d == u"hexkey") {
        // CircuitMaker's keys: framed squares apart, the held one's number in red
        const double cw = body.width() / 4, ch = body.height() / 4;
        for (int k = 0; k < 16; ++k) {
            const QRectF cell(body.left() + (k % 4) * cw, body.top() + keyRow(k / 4) * ch, cw, ch);
            const QRectF key = cell.adjusted(7, 7, -7, -7);
            p->setPen(QPen(ink, 1));
            p->setBrush(QColor(0xf4, 0xf4, 0xf4));
            p->drawRect(key);
            p->setBrush(Qt::NoBrush);
            p->drawRect(key.adjusted(3, 3, -3, -3));
            SymbolPainter::uprightText(p, cell.center(), hexDigit(k), 14, 0, state == k ? QColor(0xe0, 0x10, 0x10) : ink);
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
    } else if (d == u"car") {
        // the car beside its box at the start, its distance to the right at the finish
        const double x = -45 + std::clamp(state, 0, 100) / 100.0 * carTravel();
        // drawn in its colour, as CircuitMaker draws it
        const QString colour = m_c.props.value(QStringLiteral("color"), QStringLiteral("red")).toString();
        const QColor body = ledColor(colour);
        p->setPen(QPen(body, 2));
        p->setBrush(Qt::white);
        QPainterPath car;
        car.moveTo(x, 10);
        car.lineTo(x, -6);
        car.lineTo(x + 30, -14);
        car.lineTo(x + 55, -30);
        car.lineTo(x + 115, -30);
        car.lineTo(x + 140, -10);
        car.lineTo(x + 165, -6);
        car.lineTo(x + 165, 10);
        car.closeSubpath();
        p->drawPath(car);
        p->setBrush(Qt::white);
        p->drawPolygon(QPolygonF({QPointF(x + 62, -24), QPointF(x + 84, -24), QPointF(x + 84, -12), QPointF(x + 48, -12)}));
        p->drawPolygon(QPolygonF({QPointF(x + 92, -24), QPointF(x + 112, -24), QPointF(x + 128, -12), QPointF(x + 92, -12)}));
        for (const double wx : {x + 35, x + 130}) {
            p->drawEllipse(QPointF(wx, 10), 15, 15);
            p->drawEllipse(QPointF(wx, 10), 6, 6);
        }
    } else if (d == u"rocket") {
        // on the pad (height 0) or climbing; out of sight at 100
        const int h = std::clamp(state, 0, 100);
        if (h < 100) {
            const double y = 10 - h * 2.8;
            p->setPen(QPen(ink, 1.5));
            p->setBrush(QColor(0xe8, 0xe8, 0xe8));
            p->drawPolygon(QPolygonF({QPointF(-10, y - 60), QPointF(0, y - 80), QPointF(10, y - 60), QPointF(10, y),
                                      QPointF(-10, y)}));
            p->drawPolygon(QPolygonF({QPointF(-10, y - 15), QPointF(-20, y), QPointF(-10, y)}));
            p->drawPolygon(QPolygonF({QPointF(10, y - 15), QPointF(20, y), QPointF(10, y)}));
            if (live && h > 0) {
                p->setPen(Qt::NoPen);
                p->setBrush(QColor(0xff, 0x90, 0x20));
                p->drawPolygon(QPolygonF({QPointF(-7, y), QPointF(0, y + 22), QPointF(7, y)}));
            }
        }
    } else if (d == u"contact") {
        // a relay contact: the slash shows it closed
        const bool operated = live && state == 1;
        if (m_symbol->model.value(u"closed").toBool() != operated) {
            p->setPen(QPen(operated ? theme().high : ink, 1.5));
            p->drawLine(QPointF(-14, 14), QPointF(14, -14));
        }
    } else if (d == u"motor") {
        // a DC motor turns while energized, as CircuitMaker animates it
        if (live && state > 0) {
            const SchematicScene* sc = schematic();
            const double a = double((sc ? sc->simTime() : 0) % 24) * 15 * M_PI / 180;
            const QPointF tip(24 * std::cos(a), 24 * std::sin(a));
            p->setPen(QPen(theme().high, 3, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(-tip, tip);
        }
    } else if (d == u"buzzer") {
        // sound waves while it sounds
        if (live && state > 0) {
            p->setPen(QPen(theme().high, 2));
            p->setBrush(Qt::NoBrush);
            for (int r : {38, 48})
                for (double start : {-40.0, 140.0})
                    p->drawArc(QRectF(-r, -r, 2 * r, 2 * r), int(start * 16), 80 * 16);
        }
    } else if (d == u"stepper") {
        // the rotor points at the energized coils (half steps of 45 degrees)
        const double a = (state * 45.0 - 90) * M_PI / 180;
        const QPointF tip(48 * std::cos(a), 48 * std::sin(a));
        p->setPen(QPen(live ? theme().high : ink, 4, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(-tip * 0.5, tip);
        p->setPen(QPen(ink, 1.5));
        p->setBrush(ink);
        p->drawEllipse(QPointF(), 5, 5);
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
