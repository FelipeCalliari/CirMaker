#include "library/LibraryRegistry.h"

#include "core/Circuit.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <cmath>
#include <functional>

using namespace Qt::StringLiterals;

namespace cm {

int packagePart(const SymbolDef& s, const Component& c)
{
    return s.parts.isEmpty() ? 1 : std::clamp(c.props.value(QStringLiteral("part"), 1).toInt(), 1, int(s.parts.size()));
}

QString packagePin(const SymbolDef& s, const Component& c, int pin)
{
    // the component's own numbering ("A=3,B=4,Y=5"), as edited in its properties
    if (pin >= 0 && pin < s.pins.size()) {
        const QString own = c.props.value(QStringLiteral("pins")).toString();
        for (const QString& pair : own.split(u',', Qt::SkipEmptyParts)) {
            const int eq = pair.lastIndexOf(u'=');
            if (eq > 0 && pair.left(eq).trimmed() == s.pins[pin].name) return pair.mid(eq + 1).trimmed();
        }
    }
    if (!s.parts.isEmpty())
        return s.parts[packagePart(s, c) - 1].value(pin);
    return pin >= 0 && pin < s.pins.size() ? s.pins[pin].number : QString();
}

QString shownDesignator(const SymbolDef& s, const Component& c)
{
    if (s.parts.size() < 2 || c.designator.isEmpty())
        return c.designator;
    return c.designator + QChar(u'A' + packagePart(s, c) - 1);
}


namespace {

QVector<double> numbers(const QJsonValue& v)
{
    QVector<double> out;
    for (const QJsonValue& x : v.toArray())
        out << x.toDouble();
    return out;
}

SymbolPin::Dir parseDir(const QString& s, SymbolPin::Dir fallback)
{
    if (s == u"in") return SymbolPin::Dir::In;
    if (s == u"out") return SymbolPin::Dir::Out;
    if (s == u"inout") return SymbolPin::Dir::InOut;
    if (s == u"passive") return SymbolPin::Dir::Passive;
    return fallback;
}

Primitive line(double x1, double y1, double x2, double y2)
{
    Primitive p;
    p.type = Primitive::Type::Line;
    p.v = {x1, y1, x2, y2};
    return p;
}

Primitive circle(double cx, double cy, double r, bool fill = false)
{
    Primitive p;
    p.type = Primitive::Type::Circle;
    p.v = {cx, cy, r};
    p.fill = fill;
    return p;
}

Primitive pathPrim(const QString& d)
{
    Primitive p;
    p.type = Primitive::Type::Path;
    p.path = d;
    return p;
}

Primitive textPrim(const QString& t, double x, double y, double size, int align)
{
    Primitive p;
    p.type = Primitive::Type::Text;
    p.text = t;
    p.v = {x, y};
    p.size = size;
    p.align = align;
    return p;
}

SymbolPin pin(const QString& name, double x, double y, SymbolPin::Dir dir)
{
    SymbolPin p;
    p.name = name;
    p.pos = {x, y};
    p.dir = dir;
    return p;
}

QString n(double v) { return QString::number(v, 'g', 6); }

QRectF computeBounds(const SymbolDef& s)
{
    QRectF r;
    auto add = [&r](double x, double y) {
        const QRectF pt(x, y, 0.001, 0.001);
        r = r.isNull() ? pt : r.united(pt);
    };
    for (const Primitive& p : s.graphics) {
        switch (p.type) {
        case Primitive::Type::Line:
            add(p.v[0], p.v[1]);
            add(p.v[2], p.v[3]);
            break;
        case Primitive::Type::Rect:
            add(p.v[0], p.v[1]);
            add(p.v[0] + p.v[2], p.v[1] + p.v[3]);
            break;
        case Primitive::Type::Circle:
            add(p.v[0] - p.v[2], p.v[1] - p.v[2]);
            add(p.v[0] + p.v[2], p.v[1] + p.v[2]);
            break;
        case Primitive::Type::Path: {
            static const QRegularExpression num(QStringLiteral("-?\\d+(\\.\\d+)?"));
            QVector<double> vals;
            for (auto it = num.globalMatch(p.path); it.hasNext();)
                vals << it.next().captured().toDouble();
            for (int i = 0; i + 1 < vals.size(); i += 2)
                add(vals[i], vals[i + 1]);
            break;
        }
        case Primitive::Type::Text:
            add(p.v[0] - 4, p.v[1] - 4);
            add(p.v[0] + 4, p.v[1] + 4);
            break;
        }
    }
    for (const SymbolPin& p : s.pins)
        add(p.pos.x(), p.pos.y());
    return r.adjusted(-2, -2, 2, 2);
}

} // namespace

namespace symbolgen {

// Input pin y positions on a 10-unit grid; even counts skip the centre line.
// Input pin rows on the 10-unit grid: pitch 30 up to three inputs (as in
// CircuitMaker), 20 beyond that.
static QVector<double> inputRows(int count)
{
    if (count <= 1) return {0};
    if (count == 2) return {-30, 30};
    // CircuitMaker's spacing, also for 4 and 8 inputs; an even count sits
    // half a pitch low as in its symbols (-40, -10, 20, 50), keeping the grid
    const double pitch = 30;
    const double shift = count % 2 ? 0 : 5;
    QVector<double> rows;
    for (int i = 0; i < count; ++i)
        rows << (i - (count - 1) / 2.0) * pitch + shift;
    return rows;
}

// Distinctive-shape gates, sized like CircuitMaker's: inputs at x = -80,
// output at x = 70 (90 with an inversion bubble). The body is 80 high; when
// the inputs span more than that, the back edge is extended with straight
// lines (IEEE Std 91 style) so every input meets the body.
void gate(SymbolDef& s, const QString& op, int inputs, bool invert)
{
    const QVector<double> rows = inputRows(inputs);
    const double top = -40, bot = 40;
    const double k = 0.5523;            // cubic circle approximation
    const double pinX = op == u"buf" ? -50 : -80;
    double tip = 40;                    // output apex x

    // x where an input lead meets the body at height y
    std::function<double(double)> backX = [](double) { return -50.0; };
    double backEdge = -50;              // x of the straight back extension

    if (op == u"and") {
        const double r = 40, sx = tip - r;
        s.graphics << pathPrim(QStringLiteral("M -50 %1 L %2 %1 C %3 %1 %4 %5 %4 0 C %4 %6 %3 %7 %2 %7 L -50 %7 Z")
                                   .arg(n(top), n(sx), n(sx + k * r), n(tip), n(-k * r), n(k * r), n(bot)));
    } else if (op == u"or" || op == u"xor") {
        const double back = -55, bulge = 20;
        tip = 45;
        s.graphics << pathPrim(QStringLiteral("M %1 %2 Q %3 0 %1 %4 C %5 %4 %6 %7 %8 0 C %6 %9 %5 %2 %1 %2 Z")
                                   .arg(n(back), n(top), n(back + bulge), n(bot), n(-15), n(25), n(28), n(tip), n(-28)));
        double curve = back;
        if (op == u"xor") {
            curve = back - 10;
            s.graphics << pathPrim(QStringLiteral("M %1 %2 Q %3 0 %1 %4").arg(n(curve), n(top), n(curve + bulge), n(bot)));
        }
        backEdge = curve;
        // quadratic back curve: y = top(1-2t), x = curve + 2*bulge*t(1-t)
        backX = [=](double y) {
            if (y <= top || y >= bot)
                return curve;
            const double t = (1 - y / top) / 2;
            return curve + 2 * bulge * t * (1 - t);
        };
    } else { // buf
        tip = 30;
        s.graphics << pathPrim(QStringLiteral("M -30 -30 L %1 0 L -30 30 Z").arg(n(tip)));
        backX = [](double) { return -30.0; };
    }

    // extended back edge for inputs beyond the body
    if (rows.first() < top)
        s.graphics << line(backEdge, rows.first(), backEdge, top);
    if (rows.last() > bot)
        s.graphics << line(backEdge, bot, backEdge, rows.last());

    double outStart = tip;
    double outPin = op == u"buf" ? 50 : 70;
    if (invert) {
        s.graphics << circle(tip + 5, 0, 5);
        outStart = tip + 10;
        outPin += 20;
    }
    s.graphics << line(outStart, 0, outPin, 0);

    for (int i = 0; i < rows.size(); ++i) {
        const double y = rows[i];
        s.graphics << line(pinX, y, backX(y), y);
        s.pins << pin(QString(QChar(u'A' + i)), pinX, y, SymbolPin::Dir::In);
    }
    s.pins << pin(QStringLiteral("Y"), outPin, 0, SymbolPin::Dir::Out);
}

// Rectangular body with pins on its sides. Pin entries are names or objects
// {"name", "dir", "number", "float", "clock", "label", "bubble"}; "~X" draws an
// inversion bubble and an empty name leaves a gap. "leftStart"/"rightStart"/
// "topStart"/"bottomStart" place the first pin of a side explicitly (otherwise
// the side is centred); the body then spans the side pins.
void box(SymbolDef& s, const QJsonObject& spec)
{
    const double pitch = spec.value(u"pitch").toDouble(40);   // keep even counts on the 10 grid: use 20 or 40
    const double lead = spec.value(u"lead").toDouble(30);
    const double labelSize = spec.value(u"labelSize").toDouble(10);
    const QJsonArray left = spec.value(u"left").toArray();
    const QJsonArray right = spec.value(u"right").toArray();
    const QJsonArray topPins = spec.value(u"top").toArray();
    const QJsonArray bottomPins = spec.value(u"bottom").toArray();
    const double w = spec.value(u"width").toDouble(80);
    const double hw = w / 2;
    auto start = [&](const char* key, qsizetype count) {
        const QJsonValue v = spec.value(QLatin1String(key));
        return v.isDouble() ? v.toDouble() : -(count - 1) * pitch / 2;
    };
    const double leftStart = start("leftStart", left.size()), rightStart = start("rightStart", right.size());

    double bodyTop, bodyBottom;
    if (spec.contains(u"leftStart") || spec.contains(u"rightStart")) {
        double lo = 1e9, hi = -1e9;
        if (!left.isEmpty()) { lo = std::min(lo, leftStart); hi = std::max(hi, leftStart + (left.size() - 1) * pitch); }
        if (!right.isEmpty()) { lo = std::min(lo, rightStart); hi = std::max(hi, rightStart + (right.size() - 1) * pitch); }
        bodyTop = lo - 20;
        bodyBottom = hi + 20;
    } else {
        const double rows = std::max<qsizetype>({left.size(), right.size(), 1});
        const double h = spec.value(u"height").toDouble(std::max(rows * pitch, 2 * pitch));
        bodyTop = -h / 2;
        bodyBottom = h / 2;
    }

    Primitive body;
    body.type = Primitive::Type::Rect;
    body.v = {-hw, bodyTop, w, bodyBottom - bodyTop};
    s.graphics << body;

    if (const QString title = spec.value(u"title").toString(); !title.isEmpty())
        s.graphics << textPrim(title, 0, spec.value(u"titleY").toDouble((bodyTop + bodyBottom) / 2), 10, 0);

    enum Side { L, R, T, B };
    auto place = [&](const QJsonArray& list, Side side, double first) {
        const int count = list.size();
        for (int i = 0; i < count; ++i) {
            const QJsonValue e = list[i];
            const QJsonObject o = e.isObject() ? e.toObject() : QJsonObject{{u"name"_s, e.toString()}};
            const QString name = o.value(u"name").toString();
            if (name.isEmpty())
                continue;
            const double offset = first + i * pitch;
            const bool activeLow = name.startsWith(u'~');
            const bool bubble = activeLow && o.value(u"bubble").toBool(true);
            const bool clock = o.value(u"clock").toBool(name.endsWith(u"CLK"));
            const SymbolPin::Dir def = side == R ? SymbolPin::Dir::Out : SymbolPin::Dir::In;

            double ex = 0, ey = 0, px = 0, py = 0, dx = 0, dy = 0;
            switch (side) {
            case L: ex = -hw; ey = offset; dx = -1; break;
            case R: ex = hw; ey = offset; dx = 1; break;
            case T: ex = offset; ey = bodyTop; dy = -1; break;
            case B: ex = offset; ey = bodyBottom; dy = 1; break;
            }
            px = ex + dx * lead;
            py = ey + dy * lead;
            const double r = 5;
            if (bubble) {
                s.graphics << circle(ex + dx * r, ey + dy * r, r);
                s.graphics << line(ex + dx * 2 * r, ey + dy * 2 * r, px, py);
            } else {
                s.graphics << line(ex, ey, px, py);
            }
            if (clock) {
                // triangle pointing into the body
                const double ix = -dx, iy = -dy;      // inward
                const double tx = dy, ty = dx;        // tangent
                s.graphics << pathPrim(QStringLiteral("M %1 %2 L %3 %4 L %5 %6")
                                           .arg(n(ex + tx * 7), n(ey + ty * 7), n(ex + ix * 10), n(ey + iy * 10),
                                                n(ex - tx * 7), n(ey - ty * 7)));
            }
            const QString shown = o.value(u"label").toString(name.mid(activeLow ? 1 : 0));
            if (!shown.isEmpty()) {
                const double inset = clock ? 13 : 5;
                Primitive t;
                switch (side) {
                case L: t = textPrim(shown, ex + inset, ey, labelSize, -1); break;
                case R: t = textPrim(shown, ex - inset, ey, labelSize, 1); break;
                case T: t = textPrim(shown, ex, ey + labelSize + 1, labelSize, 0); break;
                case B: t = textPrim(shown, ex, ey - labelSize - 1, labelSize, 0); break;
                }
                t.overline = activeLow && !bubble;
                s.graphics << t;
            }
            SymbolPin p = pin(name, px, py, parseDir(o.value(u"dir").toString(), def));
            p.number = o.value(u"number").toString();
            p.floatValue = o.value(u"float").toInt(0);
            s.pins << p;
        }
    };
    place(left, L, leftStart);
    place(right, R, rightStart);
    place(topPins, T, start("topStart", topPins.size()));
    place(bottomPins, B, start("bottomStart", bottomPins.size()));
}

} // namespace symbolgen

LibraryRegistry& LibraryRegistry::instance()
{
    static LibraryRegistry registry;
    return registry;
}

void LibraryRegistry::loadBuiltin()
{
    QDir dir(QStringLiteral(":/library/symbols"));
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString& f : files) {
        QString error;
        if (!loadFile(dir.filePath(f), &error))
            qWarning("library: %s: %s", qPrintable(f), qPrintable(error));
    }
}

void LibraryRegistry::clear()
{
    m_symbols.clear();
    m_aliases.clear();
    m_order.clear();
}

bool LibraryRegistry::loadFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
    if (doc.isNull()) {
        if (error) *error = pe.errorString();
        return false;
    }
    const QJsonObject root = doc.object();
    const QString category = root.value(u"category").toString();
    for (const QJsonValue& v : root.value(u"symbols").toArray()) {
        QJsonObject o = v.toObject();
        if (!o.contains(u"category"))
            o.insert(u"category", category);
        if (!addSymbol(o, error))
            return false;
    }
    return true;
}

bool LibraryRegistry::addSymbol(const QJsonObject& o, QString* error)
{
    SymbolDef s;
    if (!parseSymbol(o, s, error))
        return false;
    if (!m_symbols.contains(s.id))
        m_order << s.id;
    for (const QString& a : std::as_const(s.aliases))
        m_aliases.insert(a.toLower(), s.id);
    if (auto it = m_symbols.find(s.id); it != m_symbols.end())
        **it = s;
    else
        m_symbols.insert(s.id, std::make_shared<SymbolDef>(s));
    return true;
}

bool LibraryRegistry::parseSymbol(const QJsonObject& o, SymbolDef& s, QString* error)
{
    s.id = o.value(u"id").toString();
    if (s.id.isEmpty()) {
        if (error) *error = QStringLiteral("symbol without id");
        return false;
    }
    s.name = o.value(u"name").toString(s.id);
    s.category = o.value(u"category").toString();
    s.prefix = o.value(u"prefix").toString(QStringLiteral("U"));
    s.description = o.value(u"description").toString();
    s.display = o.value(u"display").toString();
    s.defaultLabel = o.value(u"label").toString();
    s.netLabel = o.value(u"netLabel").toBool();
    s.hidden = o.value(u"hidden").toBool();
    s.package = o.value(u"package").toString();
    for (const QJsonValue& part : o.value(u"parts").toArray()) {
        QStringList numbers;
        for (const QJsonValue& n : part.toArray()) numbers << n.toString();
        s.parts << numbers;
    }
    s.model = o.value(u"model").toObject();
    s.spice = o.value(u"spice").toObject();
    s.defaultProps = o.value(u"props").toObject().toVariantMap();
    const QJsonObject choices = o.value(u"choices").toObject();
    for (auto it = choices.begin(); it != choices.end(); ++it) {
        QStringList values;
        for (const QJsonValue& v : it.value().toArray()) values << v.toString();
        s.propChoices.insert(it.key(), values);
    }
    for (const QJsonValue& a : o.value(u"aliases").toArray())
        s.aliases << a.toString();
    for (const QJsonValue& a : o.value(u"cmPins").toArray())
        s.cmPins << a.toString();

    const QJsonObject shape = o.value(u"shape").toObject();
    const QString shapeType = shape.value(u"type").toString();
    if (shapeType == u"gate") {
        const QString op = shape.value(u"op").toString();
        const bool invert = shape.value(u"invert").toBool();
        symbolgen::gate(s, op, shape.value(u"inputs").toInt(op == u"buf" ? 1 : 2), invert);
        if (s.model.isEmpty())
            s.model = QJsonObject{{u"kind"_s, u"gate"_s}, {u"op"_s, op}, {u"invert"_s, invert}};
    } else if (shapeType == u"box") {
        symbolgen::box(s, shape);
    } else if (!shapeType.isEmpty()) {
        if (error) *error = QStringLiteral("%1: unknown shape '%2'").arg(s.id, shapeType);
        return false;
    }

    for (const QJsonValue& gv : o.value(u"graphics").toArray()) {
        const QJsonObject g = gv.toObject();
        Primitive p;
        if (g.contains(u"line")) {
            p.type = Primitive::Type::Line;
            p.v = numbers(g.value(u"line"));
        } else if (g.contains(u"rect")) {
            p.type = Primitive::Type::Rect;
            p.v = numbers(g.value(u"rect"));
        } else if (g.contains(u"circle")) {
            p.type = Primitive::Type::Circle;
            p.v = numbers(g.value(u"circle"));
        } else if (g.contains(u"path")) {
            p.type = Primitive::Type::Path;
            p.path = g.value(u"path").toString();
        } else if (g.contains(u"text")) {
            p.type = Primitive::Type::Text;
            p.text = g.value(u"text").toString();
            p.v = numbers(g.value(u"at"));
            p.size = g.value(u"size").toDouble(7);
            const QString al = g.value(u"align").toString();
            p.align = al == u"left" ? -1 : al == u"right" ? 1 : 0;
        } else {
            continue;
        }
        const int need = p.type == Primitive::Type::Line || p.type == Primitive::Type::Rect ? 4
                       : p.type == Primitive::Type::Circle ? 3
                       : p.type == Primitive::Type::Text ? 2 : 0;
        if (p.v.size() < need) {
            if (error) *error = QStringLiteral("%1: malformed graphic").arg(s.id);
            return false;
        }
        p.fill = g.value(u"fill").toBool();
        p.actuator = g.value(u"actuator").toBool();
        s.graphics << p;
    }

    for (const QJsonValue& pv : o.value(u"pins").toArray()) {
        const QJsonObject po = pv.toObject();
        const QVector<double> at = numbers(po.value(u"at"));
        if (at.size() < 2) {
            if (error) *error = QStringLiteral("%1: pin without position").arg(s.id);
            return false;
        }
        SymbolPin p = pin(po.value(u"name").toString(), at[0], at[1],
                          parseDir(po.value(u"dir").toString(), SymbolPin::Dir::Passive));
        p.number = po.value(u"number").toString();
        p.floatValue = po.value(u"float").toInt(0);
        s.pins << p;
    }

    // TTL inputs float high
    if (o.contains(u"floatInputs")) {
        const int fv = o.value(u"floatInputs").toInt();
        for (SymbolPin& p : s.pins)
            if (p.dir == SymbolPin::Dir::In)
                p.floatValue = fv;
    }

    s.bounds = computeBounds(s);
    return true;
}

const SymbolDef* LibraryRegistry::find(const QString& id) const
{
    auto it = m_symbols.constFind(id);
    return it == m_symbols.cend() ? nullptr : it->get();
}

const SymbolDef* LibraryRegistry::findByAlias(const QString& cmName) const
{
    QString key = cmName.trimmed().toLower();
    if (key.endsWith(u'~'))
        key.chop(1);
    auto it = m_aliases.constFind(key);
    return it == m_aliases.cend() ? nullptr : find(*it);
}

QList<const SymbolDef*> LibraryRegistry::all() const
{
    QList<const SymbolDef*> out;
    for (const QString& id : m_order)
        out << m_symbols.constFind(id)->get();
    return out;
}

void LibraryRegistry::removeSymbol(const QString& id)
{
    m_symbols.remove(id);
    m_order.removeAll(id);
    for (auto it = m_aliases.begin(); it != m_aliases.end();)
        it = it.value() == id ? m_aliases.erase(it) : std::next(it);
}

QStringList LibraryRegistry::categories() const
{
    QStringList out;
    for (const QString& id : m_order) {
        const QString& c = (*m_symbols.constFind(id))->category;
        if (!out.contains(c))
            out << c;
    }
    return out;
}

QTransform LibraryRegistry::placement(const Component& c)
{
    QTransform t;
    t.translate(c.pos.x(), c.pos.y());
    t.rotate(90.0 * c.rotation);
    if (c.mirrored)
        t.scale(-1, 1);
    return t;
}

QPointF LibraryRegistry::pinScenePos(const Component& c, int pinIdx) const
{
    const SymbolDef* s = find(c.symbolId);
    if (!s || pinIdx < 0 || pinIdx >= s->pins.size())
        return c.pos;
    const QPointF p = placement(c).map(s->pins[pinIdx].pos);
    return {std::round(p.x()), std::round(p.y())};
}

} // namespace cm
