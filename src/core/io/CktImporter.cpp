// CircuitMaker 2000 "CircuitMaker Text" reader.
//
// Layout recovered from cirmaker.exe (FUN_00445629 reads the file,
// FUN_0044877f the device/wire/text lists, FUN_0044e997 and FUN_0044c0ba one
// device record). The file is a stream of whitespace-separated tokens read
// with fscanf in Windows text mode; strings are "<length> <chars>", where the
// length counts an embedded newline as two characters (CR LF, stored on disk
// as CR CR LF). Normalising CR LF to LF first reproduces text-mode reads.
//
// Records:
//   device: name; type x y angle pinMax; [v>4.011: extra]; nets[0..pinMax];
//           string; 4 ints; value(+rect); designator(+rect); string(+rect);
//           3 strings; [v>4.0: kind, package]; count; ints[0..count];
//           8 ints; prefix; [v>4.011: 3 ints (n1 last), n1 ints, n2, n2 ints, 0
//           | else 4 ints]. Types 0x50/0xAA are followed by A-P encoded data.
//   wire:   12 ints (pinA, pinB, net, ..., devA, devB, ..., pointCount) and
//           pointCount "x y" pairs. pin/dev are 1-based; a negative pin is a
//           bus entry; pinA -54/-51 marks a bus line.
//   text:   LOGFONT (13 ints), face name, 4 ints, 2 rects, text.

#include "core/io/CktImporter.h"

#include "core/NetlistBuilder.h"
#include "core/WireGeometry.h"
#include "library/LibraryRegistry.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cm::CktImporter {

namespace {

// CircuitMaker's 3-unit grid maps onto our 10-unit grid.
constexpr double kScale = 10.0 / 3.0;

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Reader {
public:
    Reader(const QString& d, int pos) : m_d(d), m_p(pos) {}

    QString token()
    {
        const int n = int(m_d.size());
        while (m_p < n && m_d[m_p].isSpace()) ++m_p;
        const int start = m_p;
        while (m_p < n && !m_d[m_p].isSpace()) ++m_p;
        if (start == m_p) throw ParseError("unexpected end of file");
        return m_d.mid(start, m_p - start);
    }
    int integer()
    {
        const QString t = token();
        bool ok = false;
        const double v = t.toDouble(&ok);
        if (!ok) throw ParseError(qPrintable(QStringLiteral("number expected, found '%1'").arg(t.left(20))));
        return int(v);
    }
    QVector<int> ints(int count)
    {
        if (count < 0 || count > 100000) throw ParseError("bad count");
        QVector<int> out(count);
        for (int& v : out) v = integer();
        return out;
    }
    QString string()
    {
        const int n = integer();
        if (n <= 0) return {};
        ++m_p;                                  // the separating space
        if (m_p + n > m_d.size()) throw ParseError("string past end of file");
        QString s = m_d.mid(m_p, n);
        m_p += n;
        s.remove(u'\r');
        return s;
    }

private:
    const QString& m_d;
    int m_p;
};

struct Device {
    QString name;
    int type = 0, x = 0, y = 0, angle = 0, flags = 0;
    std::optional<QRectF> valueRect, designatorRect;
    QVector<int> nets;
    QString value, designator, prefix;
    QVector<int> data;
};

struct CktWire {
    QVector<int> h;
    QVector<QPointF> points;
    // a bus line is attached to no device and carries its name in h[0]
    bool isBus() const { return h[7] == 0 && h[8] == 0 && h[0] < 0 && h[1] == 0; }
};

// Bus and bus-entry names are stored as negative numbers: the name read as
// base-64 digits (0-9, A-Z, a-z) minus 64^length. -31 = "X", -2431 = "Q1",
// -54 = "A".
QString decodeName(int value)
{
    if (value >= 0) return {};
    qint64 v = value;
    int n = 1;
    qint64 base = 64;
    while (v + base < 0 && n < 5) { base *= 64; ++n; }
    qint64 code = v + base;
    QString out;
    for (int i = 0; i < n; ++i) {
        const int d = int(code % 64);
        code /= 64;
        QChar c;
        if (d < 10) c = QChar(u'0' + d);
        else if (d < 36) c = QChar(u'A' + d - 10);
        else if (d < 62) c = QChar(u'a' + d - 36);
        else c = u'_';
        out.prepend(c);
    }
    return out;
}

struct CktText {
    QRectF rect;
    QString text;
    int fontHeight = 13;
};

struct CktFile {
    double version = 0;
    QVector<Device> devices;
    QVector<CktWire> wires;
    QVector<CktText> texts;
};

Device readDevice(Reader& r, double version)
{
    const bool modern = version > 4.011;
    Device d;
    d.name = r.string();
    const QVector<int> head = r.ints(5);
    d.type = head[0];
    d.x = head[1];
    d.y = head[2];
    const int pinMax = head[4];
    if (modern) {
        r.integer();
        d.nets = r.ints(pinMax + 1);
        if (d.type == 0xc3)
            r.ints(r.integer());
    } else {
        d.nets = r.ints(std::abs(pinMax) + 1);
        if (pinMax < 0)
            r.ints(20);
    }
    r.string();
    const QVector<int> flags = r.ints(4);
    d.flags = flags[2];
    d.angle = flags[3];
    auto rect = [&r] {
        const QVector<int> v = r.ints(4);
        return QRectF(QPointF(v[0], v[1]), QPointF(v[2], v[3]));
    };
    d.value = r.string();
    if (!d.value.isEmpty()) d.valueRect = rect();
    d.designator = r.string();
    if (!d.designator.isEmpty()) d.designatorRect = rect();
    if (!r.string().isEmpty()) r.ints(4);
    r.string();                                 // power pins, e.g. "DVCC=14;DGND=7;"
    r.string();                                 // SPICE template
    r.string();
    if (version > 4.0) {
        r.string();                             // "type:digital"
        r.string();                             // package
    }
    r.ints(r.integer() + 1);                    // package pin map
    r.ints(8);
    d.prefix = r.string();
    if (modern) {
        const QVector<int> t = r.ints(3);
        r.ints(t[2]);
        r.ints(r.integer());
        if (r.integer() != 0)
            throw ParseError("unsupported device extension block");
    } else {
        r.ints(4);
    }
    if (d.type == 0x50 || d.type == 0xaa) {
        int remaining = d.type == 0x50 ? 32 : d.nets.value(17);
        while (remaining > 0) {
            const QString line = r.token();
            for (int i = 0; i + 1 < line.size() && remaining > 0; i += 2) {
                const QChar hi = line[i], lo = line[i + 1];
                if (hi < u'A' || hi > u'P') break;
                d.data << ((hi.unicode() - 'A') << 4 | (lo.unicode() - 'A'));
                --remaining;
            }
        }
    }
    return d;
}

CktFile parseAt(const QString& d, int pos, double version)
{
    Reader r(d, pos);
    CktFile f;
    f.version = version;
    const int devices = r.integer();
    for (int i = 0; i < devices; ++i)
        f.devices << readDevice(r, version);
    const int wires = r.integer();
    for (int i = 0; i < wires; ++i) {
        CktWire w;
        w.h = r.ints(12);
        for (int k = 0, n = w.h[11]; k < n; ++k) {
            const int x = r.integer(), y = r.integer();
            w.points << QPointF(x, y);
        }
        if (w.points.isEmpty()) throw ParseError("wire without points");
        f.wires << w;
    }
    const int texts = r.integer();
    for (int i = 0; i < texts; ++i) {
        CktText t;
        t.fontHeight = std::abs(r.ints(13)[0]);
        r.string();
        r.ints(4);
        const QVector<int> rect = r.ints(4);
        r.ints(4);
        t.rect = QRectF(QPointF(rect[0], rect[1]), QPointF(rect[2], rect[3]));
        t.text = r.string();
        f.texts << t;
    }
    return f;
}

// The header differs between versions; the device list is the first place
// where "<count>\n<len> <name>" parses all the way through the text list.
CktFile parse(const QByteArray& raw)
{
    QString d = QString::fromLatin1(raw);
    d.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QStringList head = d.left(200).split(u'\n');
    if (head.size() < 2 || !head[0].startsWith(u"CircuitMaker Text"))
        throw ParseError("not a CircuitMaker text schematic");
    bool ok = false;
    const double version = head[1].trimmed().toDouble(&ok);
    if (!ok) throw ParseError("unreadable version");
    if (version > 5.61) throw ParseError("version newer than CircuitMaker 2000 (5.61)");

    static const QRegularExpression candidate(QStringLiteral("(?m)^(\\d+)\\n(\\d+) "));
    QString lastError = QStringLiteral("device list not found");
    for (auto it = candidate.globalMatch(d); it.hasNext();) {
        const auto m = it.next();
        if (m.captured(1).toInt() <= 0) continue;
        try {
            return parseAt(d, int(m.capturedStart()), version);
        } catch (const ParseError& e) {
            lastError = QString::fromUtf8(e.what());
        }
    }
    throw ParseError(qPrintable(lastError));
}

QPointF scaled(QPointF p) { return {std::round(p.x() * kScale / 10) * 10, std::round(p.y() * kScale / 10) * 10}; }

int rotationFromAngle(int angle)
{
    switch (angle) {
    case 90: return 3;      // CircuitMaker turns counter-clockwise
    case 180: return 2;
    case 270: return 1;
    default: return 0;
    }
}

// "Input"/"Output" devices keep their name as character codes in nets[10..].
QString decodeName(const QVector<int>& nets)
{
    QString s;
    for (int i = 10; i < nets.size() && nets[i] > 0 && nets[i] < 128; ++i)
        s += QChar(nets[i]);
    return s.trimmed();
}

// Value with a SPICE scale suffix ("1m", "4.7k"); 0 when unreadable.
double spiceValue(const QString& t)
{
    static const QRegularExpression re(QStringLiteral("^([-+]?[\\d.]+(?:[eE][-+]?\\d+)?)([a-zA-Z]*)"));
    const auto m = re.match(t.trimmed());
    if (!m.hasMatch()) return 0;
    double v = m.captured(1).toDouble();
    const QString suf = m.captured(2).toLower();
    if (suf.startsWith(u"meg")) return v * 1e6;
    if (suf.isEmpty()) return v;
    switch (suf[0].unicode()) {
    case 'p': return v * 1e-12;
    case 'n': return v * 1e-9;
    case 'u': return v * 1e-6;
    case 'm': return v * 1e-3;
    case 'k': return v * 1e3;
    default: return v;
    }
}

// Which side of a bus entry its label goes: towards the wire it ends.
QString entrySide(QPointF at, QPointF from)
{
    const QPointF d = from - at;
    if (std::abs(d.x()) >= std::abs(d.y()))
        return d.x() < 0 ? QStringLiteral("left") : QStringLiteral("right");
    return d.y() < 0 ? QStringLiteral("up") : QStringLiteral("down");
}

int ourPin(const SymbolDef& s, int cmPin)
{
    if (cmPin < 1) return -1;
    if (!s.cmPins.isEmpty()) {
        const QString name = s.cmPins.value(cmPin - 1);
        return name.isEmpty() ? -1 : s.pinIndex(name);
    }
    return cmPin - 1 < s.pins.size() ? cmPin - 1 : -1;
}

double median(QVector<double> v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

bool import(const QString& path, Circuit& circuit, const LibraryRegistry& library, CktImportReport* report, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    return importData(f.readAll(), circuit, library, report, error);
}

bool importData(const QByteArray& data, Circuit& circuit, const LibraryRegistry& library, CktImportReport* report,
                QString* error)
{
    CktFile file;
    try {
        file = parse(data);
    } catch (const ParseError& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    CktImportReport local;
    CktImportReport& rep = report ? *report : local;
    rep = {};
    rep.version = file.version;

    circuit.clear();
    const SymbolDef* unknown = library.find(QStringLiteral("unknown"));
    const SymbolDef* netLabel = library.find(QStringLiteral("netlabel"));

    // ---- components: symbol, orientation, properties
    struct Placed { ItemId id = 0; const SymbolDef* symbol = nullptr; Component comp; };
    QVector<Placed> placed(file.devices.size());
    for (int i = 0; i < file.devices.size(); ++i) {
        const Device& d = file.devices[i];
        QString cmName = d.name;
        if (cmName.endsWith(u'~')) cmName.chop(1);
        // analysis directives and instruments have no schematic meaning here
        if (cmName.startsWith(u"SAVE"))
            continue;
        const SymbolDef* s = library.findByAlias(cmName);
        Component c;
        c.designator = d.designator;
        c.rotation = rotationFromAngle(d.angle);
        c.pos = scaled(QPointF(d.x, d.y));
        if (!s) {
            rep.unmapped[cmName]++;
            s = unknown;
            c.label = cmName;
        } else if (cmName == u"Input" || cmName == u"Output") {
            c.label = decodeName(d.nets);
        } else if (s->netLabel || s->prefix == u"U" || !s->defaultLabel.isEmpty()) {
            c.label = d.value;
        } else {
            c.label = s->defaultLabel;
        }
        c.symbolId = s->id;

        const QString kind = s->model.value(u"kind").toString();
        if (kind == u"logicswitch")
            c.props.insert(QStringLiteral("state"), int(d.value.trimmed() == u"5V"));
        else if (kind == u"pulser" && d.nets.size() > 8 && d.nets[7] > 0 && d.nets[8] > 0)
            c.props.insert(QStringLiteral("high"), d.nets[7]), c.props.insert(QStringLiteral("low"), d.nets[8]);
        else if (kind == u"pot") {
            // "100k 40%": total resistance and wiper position
            c.label = d.value.section(u' ', 0, 0);
            static const QRegularExpression percent(QStringLiteral("(\\d+(?:\\.\\d+)?)\\s*%"));
            if (const auto m = percent.match(d.value); m.hasMatch())
                c.props.insert(QStringLiteral("position"), m.captured(1).toDouble());
        } else if (kind == u"siggen") {
            // "-1/1V": low/high levels
            static const QRegularExpression levels(QStringLiteral("^\\s*([-+]?[\\d.]+[a-zA-Z]*)\\s*/\\s*([-+]?[\\d.]+[a-zA-Z]*)"));
            if (const auto m = levels.match(d.value); m.hasMatch()) {
                const double lo = spiceValue(m.captured(1)), hi = spiceValue(m.captured(2));
                c.props.insert(QStringLiteral("amplitude"), QString::number((hi - lo) / 2));
                c.props.insert(QStringLiteral("offset"), QString::number((hi + lo) / 2));
            }
            c.label = s->defaultLabel;
        } else if (kind == u"rom" && !d.data.isEmpty()) {
            QStringList bytes;
            for (int b : d.data)
                bytes << QStringLiteral("%1").arg(b, 2, 16, QChar(u'0')).toUpper();
            c.props.insert(QStringLiteral("data"), bytes.join(u' '));
        } else if (kind == u"dataseq" && d.nets.size() > 17) {
            // nets[11..17]: present address, start, stop, tick increment, (pattern
            // increment), external clock, line count + 1. Addresses count from 1;
            // data byte 0 is unused.
            QStringList bytes;
            for (int b = 1; b < d.data.size(); ++b)
                bytes << QStringLiteral("%1").arg(d.data[b], 2, 16, QChar(u'0')).toUpper();
            const int lines = std::max(1, d.nets[17] - 1);
            c.props.insert(QStringLiteral("data"), bytes.join(u' '));
            c.props.insert(QStringLiteral("lines"), lines);
            c.props.insert(QStringLiteral("start"), std::clamp(d.nets[12] - 1, 0, lines - 1));
            c.props.insert(QStringLiteral("stop"), std::clamp(d.nets[13] - 1, 0, lines - 1));
            c.props.insert(QStringLiteral("tick"), std::max(1, d.nets[14]));
            c.props.insert(QStringLiteral("external"), d.nets[16] ? 1 : 0);
        } else if (kind == u"key" && d.nets.size() > 11 && d.nets[11] > 0) {
            const int code = d.nets[11];
            const int bits = s->model.value(u"bits").toInt(4);
            c.props.insert(QStringLiteral("value"), bits == 4 ? QString(QChar(code)).toInt(nullptr, 16) : code & 0x7f);
        }
        placed[i] = {0, s, c};
    }

    // circuits with parts that only make sense in SPICE open in analog mode
    int analogOnly = 0;
    for (const Placed& p : std::as_const(placed))
        if (p.symbol && !p.symbol->spice.isEmpty() && p.symbol->model.isEmpty())
            ++analogOnly;
    for (const Placed& p : std::as_const(placed))
        if (p.symbol && (p.symbol->model.value(u"kind").toString() == u"siggen"))
            ++analogOnly;
    if (analogOnly > 0) {
        AnalysisSettings a;
        a.analogMode = true;
        circuit.setAnalysis(a);
    }

    // ---- placement: align our pins with the wire ends attached to CM pins
    QVector<QVector<QPointF>> anchorDeltas(file.devices.size());
    for (const CktWire& w : file.wires) {
        if (w.isBus()) continue;
        const int ends[2][2] = {{w.h[7], w.h[0]}, {w.h[8], w.h[1]}};
        for (int e = 0; e < 2; ++e) {
            const int dev = ends[e][0] - 1, cmPin = ends[e][1];
            if (dev < 0 || dev >= placed.size() || !placed[dev].symbol) continue;
            const int pin = ourPin(*placed[dev].symbol, cmPin);
            if (pin < 0) continue;
            Component probe = placed[dev].comp;
            probe.pos = {};
            const QPointF offset = LibraryRegistry::placement(probe).map(placed[dev].symbol->pins[pin].pos);
            const QPointF end = scaled(e == 0 ? w.points.first() : w.points.last());
            anchorDeltas[dev] << end - offset;
        }
    }
    for (int i = 0; i < placed.size(); ++i) {
        if (!placed[i].symbol) continue;
        if (!anchorDeltas[i].isEmpty()) {
            QVector<double> xs, ys;
            for (QPointF p : anchorDeltas[i]) { xs << p.x(); ys << p.y(); }
            placed[i].comp.pos = QPointF(std::round(median(xs) / 10) * 10, std::round(median(ys) / 10) * 10);
        }
        // text as CircuitMaker showed it: flag 0x200 designator, 0x100 label
        const Device& d = file.devices[i];
        Component& comp = placed[i].comp;
        comp.showDesignator = (d.flags & 0x200) != 0;
        comp.showLabel = (d.flags & 0x100) != 0;
        const QPointF origin(d.x, d.y);
        if (d.designatorRect)
            comp.designatorAt = (origin + d.designatorRect->center()) * kScale - comp.pos;
        if (d.valueRect)
            comp.labelAt = (origin + d.valueRect->center()) * kScale - comp.pos;
        placed[i].id = circuit.addComponent(comp);
    }

    // ---- wires: scale, then pin attached ends to our pins
    QHash<ItemId, int> wireNetOf;               // our wire -> CM net
    struct Entry { QPointF at; QPointF from; QString name; };
    QVector<Entry> entries;                     // wire ends on a bus, named as in CircuitMaker
    for (const CktWire& w : file.wires) {
        Wire out;
        out.bus = w.isBus();
        if (out.bus)
            out.name = decodeName(w.h[0]);
        for (QPointF p : w.points)
            out.points << scaled(p);
        if (!out.bus) {
            const int ends[2][2] = {{w.h[7], w.h[0]}, {w.h[8], w.h[1]}};
            for (int e = 0; e < 2; ++e) {
                const int dev = ends[e][0] - 1, cmPin = ends[e][1];
                if (dev < 0 || dev >= placed.size() || !placed[dev].id) continue;
                const int pin = ourPin(*placed[dev].symbol, cmPin);
                if (pin < 0) continue;
                const Component* c = circuit.component(placed[dev].id);
                const int idx = e == 0 ? 0 : int(out.points.size()) - 1;
                const QPointF target = library.pinScenePos(*c, pin);
                if (!WireGeometry::near(out.points[idx], target))
                    WireGeometry::moveEnd(out.points, idx, target - out.points[idx]);
            }
        }
        out.points = WireGeometry::simplify(out.points);
        if (out.points.size() == 1)
            out.points << out.points.first();
        const ItemId id = circuit.addWire(out);
        if (!out.bus && w.h[2] > 0)
            wireNetOf.insert(id, w.h[2]);
        if (!out.bus) {
            if (w.h[0] < 0 && w.h[7] == 0)
                entries << Entry{out.points.first(), out.points.value(1), decodeName(w.h[0])};
            if (w.h[1] < 0 && w.h[8] == 0)
                entries << Entry{out.points.last(), out.points.value(out.points.size() - 2), decodeName(w.h[1])};
        }
    }
    QSet<std::pair<qint64, qint64>> entryAt;
    for (const Entry& e : std::as_const(entries)) {
        if (!netLabel || e.name.isEmpty()) continue;
        const std::pair<qint64, qint64> key{qRound64(e.at.x()), qRound64(e.at.y())};
        if (entryAt.contains(key)) continue;
        entryAt.insert(key);
        Component label;
        label.symbolId = netLabel->id;
        label.pos = e.at;
        label.label = e.name;
        label.props.insert(QStringLiteral("side"), entrySide(e.at, e.from));
        circuit.addComponent(label);
    }

    for (const CktText& t : file.texts) {
        if (t.text.trimmed().isEmpty()) continue;
        circuit.addText({0, scaled(t.rect.topLeft()), t.text, std::max(6.0, t.fontHeight * kScale * 0.6)});
    }

    // ---- connectivity check: every CM net must be one of our nets
    const Netlist nl = NetlistBuilder::build(circuit, library);
    struct Member { int ourNet; QPointF at; bool pin; };
    QHash<int, QVector<Member>> byCmNet;
    QHash<int, QString> netName;                // CM net -> terminal label on it
    for (int i = 0; i < placed.size(); ++i) {
        if (!placed[i].id) continue;
        const Device& d = file.devices[i];
        const SymbolDef& s = *placed[i].symbol;
        const Component* c = circuit.component(placed[i].id);
        for (int cmPin = 1; cmPin < d.nets.size(); ++cmPin) {
            const int net = d.nets[cmPin];
            const int pin = ourPin(s, cmPin);
            if (net <= 0 || pin < 0) continue;
            byCmNet[net] << Member{nl.netOf(c->id, pin), library.pinScenePos(*c, pin), true};
            if (s.netLabel && !c->label.isEmpty())
                netName.insert(net, c->label);
        }
    }
    for (auto it = wireNetOf.cbegin(); it != wireNetOf.cend(); ++it)
        byCmNet[it.value()] << Member{nl.wireNet.value(it.key(), -1), circuit.wire(it.key())->points.first(), false};

    QHash<int, QSet<int>> cmNetsOfOurNet;
    for (auto it = byCmNet.cbegin(); it != byCmNet.cend(); ++it)
        for (const Member& m : it.value())
            if (m.ourNet >= 0) cmNetsOfOurNet[m.ourNet].insert(it.key());
    int shorts = 0;
    for (const QSet<int>& s : std::as_const(cmNetsOfOurNet))
        shorts += s.size() > 1;

    // Connections the redrawn symbols lost become real wires: an L from the
    // stray group to the nearest point of the net's largest group, as long as
    // it touches nothing of another net. Only when no clean path exists does
    // a visible net label pair join them.
    const Netlist nl2 = NetlistBuilder::build(circuit, library);
    struct Spot { QPointF at; int net; };
    QVector<Spot> spots;                        // every pin and wire vertex, with its net
    for (const Component& c : circuit.components()) {
        const SymbolDef* sym = library.find(c.symbolId);
        if (!sym) continue;
        for (int i = 0; i < sym->pins.size(); ++i)
            spots << Spot{library.pinScenePos(c, i), nl2.netOf(c.id, i)};
    }
    for (const Wire& w : circuit.wires())
        if (!w.bus)
            for (QPointF p : w.points)
                spots << Spot{p, nl2.wireNet.value(w.id, -1)};
    auto clean = [&](const QVector<QPointF>& path, int netA, int netB) {
        for (int i = 0; i + 1 < path.size(); ++i)
            for (const Spot& sp : std::as_const(spots))
                if (sp.net != netA && sp.net != netB && WireGeometry::onSegment(sp.at, path[i], path[i + 1]))
                    return false;
        for (const Wire& w : circuit.wires())       // ends must not land on foreign wires
            if (!w.bus && nl2.wireNet.value(w.id, -1) != netA && nl2.wireNet.value(w.id, -1) != netB)
                for (QPointF end : {path.first(), path.last()})
                    if (WireGeometry::onPolyline(end, w.points))
                        return false;
        return true;
    };

    int drawn = 0, labelled = 0;
    for (auto it = byCmNet.cbegin(); it != byCmNet.cend(); ++it) {
        QHash<int, QVector<QPointF>> groups;   // our net -> member points
        for (const Spot& sp : std::as_const(spots)) {
            // a spot belongs to this CM net if its net holds one of the members
            bool member = false;
            for (const Member& m : it.value())
                if (WireGeometry::near(m.at, sp.at)) { member = true; break; }
            if (member && sp.net >= 0)
                groups[sp.net] << sp.at;
        }
        // extend each group with every spot of the same net
        for (auto g = groups.begin(); g != groups.end(); ++g)
            for (const Spot& sp : std::as_const(spots))
                if (sp.net == g.key())
                    g.value() << sp.at;
        if (groups.size() < 2) continue;
        int main = -1;
        for (auto g = groups.cbegin(); g != groups.cend(); ++g)
            if (main < 0 || g.value().size() > groups.value(main).size()) main = g.key();
        for (auto g = groups.cbegin(); g != groups.cend(); ++g) {
            if (g.key() == main) continue;
            QPointF a, b;
            double best = 1e18;
            for (QPointF pa : g.value())
                for (QPointF pb : groups.value(main)) {
                    const double d = std::abs(pa.x() - pb.x()) + std::abs(pa.y() - pb.y());
                    if (d < best) { best = d; a = pa; b = pb; }
                }
            bool done = false;
            for (const QVector<QPointF>& path : {QVector<QPointF>{a, QPointF(b.x(), a.y()), b},
                                                 QVector<QPointF>{a, QPointF(a.x(), b.y()), b}}) {
                if (!clean(path, g.key(), main)) continue;
                Wire w;
                w.points = WireGeometry::simplify(path);
                if (w.points.size() < 2) w.points = {a, b};
                circuit.addWire(w);
                ++drawn;
                done = true;
                break;
            }
            if (!done && netLabel) {
                const QString name = netName.value(it.key(), QStringLiteral("N%1").arg(it.key()));
                for (QPointF at : {a, b}) {
                    Component label;
                    label.symbolId = netLabel->id;
                    label.pos = at;
                    label.label = name;
                    circuit.addComponent(label);
                }
                ++labelled;
            }
        }
    }

    rep.components = int(file.devices.size());
    rep.wires = int(file.wires.size());
    rep.texts = int(file.texts.size());
    if (drawn)
        rep.warnings << QStringLiteral("%1 wires added where the redrawn symbols no longer met the original wiring").arg(drawn);
    if (labelled)
        rep.warnings << QStringLiteral("%1 connections made with net labels because no clean wire path existed").arg(labelled);
    if (shorts)
        rep.warnings << QStringLiteral("%1 places where redrawn symbols touch wires of another net; check them").arg(shorts);
    return true;
}

} // namespace cm::CktImporter
