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
#include "core/io/CktFile.h"
#include "core/io/CmUserLib.h"
#include "core/WireGeometry.h"
#include "core/io/NativeFormat.h"
#include "library/LibraryRegistry.h"
#include "library/Macros.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QLineF>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cm::CktImporter {

namespace {

using namespace cm::ckt;

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
    d.tag = r.string();
    if (!d.tag.isEmpty()) r.ints(4);
    r.string();                                 // power pins, e.g. "DVCC=14;DGND=7;"
    r.string();                                 // SPICE template
    r.string();
    if (version > 4.0) {
        d.kind = r.string();                    // "type:digital", or a model alias with parameters
        r.string();                             // package
    }
    // package pin map: 0, the pins of this part, then the pins of every part
    const QVector<int> pkg = r.ints(r.integer() + 1);
    const int pins = std::abs(pinMax);
    if (pins > 0 && pkg.size() >= 1 + 2 * pins)
        d.packagePins = pkg.mid(1 + pins, pins);
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

int ourPin(const SymbolDef& s, const Device& d, int cmPin)
{
    if (cmPin < 1) return -1;
    if (!s.cmPins.isEmpty()) {
        const QString name = s.cmPins.value(cmPin - 1);
        return name.isEmpty() ? -1 : s.pinIndex(name);
    }
    // numbered symbols: match the package pin
    if (cmPin <= d.packagePins.size() && !s.pins.isEmpty() && !s.pins.first().number.isEmpty()) {
        const QString number = QString::number(d.packagePins[cmPin - 1]);
        for (int i = 0; i < s.pins.size(); ++i)
            if (s.pins[i].number == number) return i;
        return -1;
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

namespace {

bool importFile(const CktFile& file, Circuit& circuit, const LibraryRegistry& library, CktImportReport* report, int depth);

// The macros of CircuitMaker's User.lib, read once.
const CmUserLib& userLib()
{
    static const CmUserLib lib = [] {
        CmUserLib l;
        const QString path = CmUserLib::locate(QSettings().value(QStringLiteral("cm/folder")).toString());
        if (!path.isEmpty()) l.load(path);
        return l;
    }();
    return lib;
}

// A macro of User.lib as one of ours: its circuit, with an Input or Output
// port on the net each pin reaches (left and right of the circuit, joined by
// the importer like any connection the redrawn symbols lost), and the
// CircuitMaker pin order. Nested macros are defined on the way.
bool defineMacro(const CmMacro& m, const LibraryRegistry& library, int depth)
{
    CktFile inner = m.body;
    // wire ends on device pins whose nets disagree: the nets decide
    for (CktWire& w : inner.wires) {
        const int a = w.h[7] - 1, b = w.h[8] - 1;
        if (a < 0 || b < 0 || a >= inner.devices.size() || b >= inner.devices.size()) continue;
        if (inner.devices[a].nets.value(w.h[0], -1) != inner.devices[b].nets.value(w.h[1], -2))
            w.h[0] = w.h[1] = w.h[7] = w.h[8] = 0;
    }
    int left = 0, right = 0, top = 0;
    for (int i = 0; i < inner.devices.size(); ++i) {
        const Device& d = inner.devices[i];
        if (i == 0 || d.x < left) left = d.x;
        if (i == 0 || d.x > right) right = d.x;
        if (i == 0 || d.y < top) top = d.y;
    }
    int rows[2] = {0, 0};
    for (int k = 0; k < m.pins.size(); ++k) {
        const bool out = m.outputs.value(k);
        Device port;
        port.name = out ? QStringLiteral("Output") : QStringLiteral("Input");
        port.nets.fill(0, 10);
        port.nets[1] = m.pinNets.value(k);
        for (QChar c : m.pins[k]) port.nets << (c.unicode() < 128 ? c.unicode() : '_');
        port.nets << 0;
        port.x = out ? right + 45 : left - 45;
        port.y = top + 12 * rows[out]++;
        inner.devices << port;
    }
    Circuit c;
    CktImportReport rep;
    if (!importFile(inner, c, library, &rep, depth + 1)) return false;
    QString error;
    QJsonObject sym = macros::symbolJson(m.name.endsWith(u'~') ? m.name.chopped(1) : m.name,
                                         QJsonDocument::fromJson(NativeFormat::serialize(c)).object(), &error);
    if (sym.isEmpty()) return false;
    sym.insert(QStringLiteral("cmPins"), QJsonArray::fromStringList(m.pins));
    // pins where CircuitMaker's symbol has them, so its wiring meets them:
    // rows on a 10-unit pitch with gaps between
    QJsonObject shape = sym.value(u"shape").toObject();
    int topRow = 0;
    for (const bool right : {false, true}) {
        QMap<int, QString> rows;
        for (int k = 0; k < m.pins.size(); ++k)
            if (m.outputs.value(k) == right) rows.insert(int(std::lround(m.at.value(k).y() * kScale / 10)) * 10, m.pins[k]);
        QJsonArray column;
        if (!rows.isEmpty()) {
            for (int y = rows.firstKey(); y <= rows.lastKey(); y += 10) column.append(rows.value(y));
            topRow = std::min(topRow, rows.firstKey());
        }
        shape.insert(right ? QStringLiteral("right") : QStringLiteral("left"), column);
        shape.insert(right ? QStringLiteral("rightStart") : QStringLiteral("leftStart"), rows.isEmpty() ? 0 : rows.firstKey());
    }
    shape.insert(QStringLiteral("pitch"), 10);
    shape.insert(QStringLiteral("lead"), 30);
    shape.insert(QStringLiteral("width"), 160);       // pin ends 32 CM units out, past the drawn pins
    shape.insert(QStringLiteral("titleY"), topRow);         // the middle is often taken by the value
    sym.insert(QStringLiteral("shape"), shape);
    sym.insert(QStringLiteral("description"), QStringLiteral("CircuitMaker macro %1 (from its User.lib)").arg(m.name));
    return LibraryRegistry::instance().addSymbol(sym, &error);
}

} // namespace

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
    return importFile(file, circuit, library, report, 0);
}

namespace {

bool importFile(const CktFile& file, Circuit& circuit, const LibraryRegistry& library, CktImportReport* report, int depth)
{
    CktImportReport local;
    CktImportReport& rep = report ? *report : local;
    rep = {};
    rep.version = file.version;

    circuit.clear();
    const SymbolDef* unknown = library.find(QStringLiteral("unknown"));
    const SymbolDef* netLabel = library.find(QStringLiteral("netlabel"));

    // macros of CircuitMaker's own User.lib become ours
    if (depth < 8)
        for (const Device& d : file.devices) {
            QString cmName = d.name;
            if (cmName.endsWith(u'~')) cmName.chop(1);
            if (library.findByAlias(cmName)) continue;
            if (const CmMacro* m = userLib().find(cmName)) defineMacro(*m, library, depth);
        }

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
        // gates of a package: "U6A" is part A of U6, whose pins CircuitMaker
        // lists after the part's own ones
        if (s && s->parts.size() > 1 && c.designator.size() > 1) {
            const QChar letter = c.designator.back();
            const int part = letter.unicode() - u'A' + 1;
            if (c.designator.at(c.designator.size() - 2).isDigit() && part >= 1 && part <= s->parts.size()) {
                c.designator.chop(1);
                c.props.insert(QStringLiteral("part"), part);
            }
        }
        // the low 9 bits hold the angle and 0x200 marks a mirrored device
        // (692 = mirrored and turned 180 degrees); higher bits are left over
        // from older versions
        const int angle = d.angle & 0x1ff;
        const bool valid = angle == 0 || angle == 90 || angle == 180 || angle == 270;
        c.mirrored = valid && (d.angle & 0x200);
        c.rotation = valid ? rotationFromAngle(angle) : 0;
        // CircuitMaker mirrors after turning: a mirrored quarter turn is
        // our mirror with the opposite turn
        if (c.mirrored && (angle == 90 || angle == 270)) c.rotation = (c.rotation + 2) % 4;
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
        // model parameters given with the alias, "{Fc=100k Fr=50k}": those the part has
        static const QRegularExpression params(QStringLiteral("\\{([^}]*)\\}"));
        if (const auto m = params.match(d.kind); m.hasMatch()) {
            for (const QString& pair : m.captured(1).split(u' ', Qt::SkipEmptyParts)) {
                const int eq = pair.indexOf(u'=');
                if (eq <= 0) continue;
                for (auto it = s->defaultProps.cbegin(); it != s->defaultProps.cend(); ++it)
                    if (it.key().compare(pair.left(eq), Qt::CaseInsensitive) == 0) c.props.insert(it.key(), pair.mid(eq + 1));
            }
        }

        if (!d.tag.isEmpty() && s->defaultProps.contains(QStringLiteral("relay")))
            c.props.insert(QStringLiteral("relay"), d.tag.trimmed());
        const QString kind = s->model.value(u"kind").toString();
        if (kind == u"logicswitch")
            c.props.insert(QStringLiteral("state"), int(d.value.trimmed() == u"5V"));
        else if (kind == u"pulser" && d.nets.size() > 8 && d.nets[7] > 0 && d.nets[8] > 0)
            c.props.insert(QStringLiteral("high"), d.nets[7]), c.props.insert(QStringLiteral("low"), d.nets[8]);
        else if (kind == u"car" && d.nets.size() > 14 && d.nets[14] > 0)
            c.props.insert(QStringLiteral("speed"), d.nets[14]);      // as CircuitMaker keeps it
        else if (kind == u"pot") {
            // "100k 40%": total resistance and wiper position
            c.label = d.value.section(u' ', 0, 0);
            static const QRegularExpression percent(QStringLiteral("(\\d+(?:\\.\\d+)?)\\s*%"));
            if (const auto m = percent.match(d.value); m.hasMatch())
                c.props.insert(QStringLiteral("position"), m.captured(1).toDouble());
        } else if (s->id == u"vco" && d.value.trimmed().toUpper() == u"SQRVCO") {
            // CircuitMaker's square-wave VCO swings 0 to 5 V
            c.props.insert(QStringLiteral("low"), QStringLiteral("0"));
            c.props.insert(QStringLiteral("high"), QStringLiteral("5"));
        } else if (kind == u"siggen") {
            // "-1/1V": low/high levels
            static const QRegularExpression levels(QStringLiteral("^\\s*([-+]?[\\d.]+[a-zA-Z]*)\\s*/\\s*([-+]?[\\d.]+[a-zA-Z]*)"));
            if (const auto m = levels.match(d.value); m.hasMatch()) {
                const double lo = spiceValue(m.captured(1)), hi = spiceValue(m.captured(2));
                c.props.insert(QStringLiteral("amplitude"), QString::number((hi - lo) / 2));
                c.props.insert(QStringLiteral("offset"), QString::number((hi + lo) / 2));
            }
            c.label = s->defaultLabel;
        } else if ((kind == u"rom" || kind == u"ram") && !d.data.isEmpty()) {
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
            const int pin = ourPin(*placed[dev].symbol, file.devices[dev], cmPin);
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
        // CircuitMaker's texts sit around its own, often larger, drawings:
        // keep them on the same side but close to ours. Anchors are the
        // centre of the baseline, about 10 above it and 3 below.
        if (!placed[i].symbol->netLabel) {
            Component turned = comp;
            turned.pos = {};
            const QRectF body = LibraryRegistry::placement(turned).mapRect(placed[i].symbol->bounds);
            // one shift for both, the smallest that brings one in: stacked
            // texts keep their spacing
            std::optional<QPointF> shift;
            for (std::optional<QPointF>* at : {&comp.designatorAt, &comp.labelAt}) {
                if (!*at) continue;
                const double half = 4.5 * (at == &comp.designatorAt ? d.designator.size() : d.value.size());
                const QPointF a = **at;
                const QPointF s(std::clamp(a.x(), body.left() - 12 - half, body.right() + 12 + half) - a.x(),
                                std::clamp(a.y(), body.top() - 14, body.bottom() + 24) - a.y());
                if (!shift) shift = s;
                else shift = QPointF(std::abs(s.x()) < std::abs(shift->x()) ? s.x() : shift->x(),
                                     std::abs(s.y()) < std::abs(shift->y()) ? s.y() : shift->y());
            }
            for (std::optional<QPointF>* at : {&comp.designatorAt, &comp.labelAt})
                if (*at) *at = QPointF(std::round((**at + *shift).x()), std::round((**at + *shift).y()));
        }
        placed[i].id = circuit.addComponent(comp);
    }

    // ---- wires: scale, then pin attached ends to our pins
    QMap<ItemId, int> wireNetOf;                // our wire -> CM net (ordered: same result every run)
    struct Entry { QPointF at; QPointF from; QString name; int cmNet; };
    QVector<Entry> entries;                     // wire ends on a bus, named as in CircuitMaker
    for (const CktWire& w : file.wires) {
        // a zero-length leftover touching no device connects nothing in
        // CircuitMaker, but would join whatever wire it lies on here
        if (w.h[7] == 0 && w.h[8] == 0 && !w.isBus()
            && std::all_of(w.points.cbegin(), w.points.cend(), [&](QPointF p) { return p == w.points.first(); }))
            continue;
        Wire out;
        out.bus = w.isBus();
        if (out.bus)
            out.name = w.h[0] < 0 ? decodeName(w.h[0]) : QString::number(w.h[0]);
        for (QPointF p : w.points)
            out.points << scaled(p);
        if (!out.bus) {
            const int ends[2][2] = {{w.h[7], w.h[0]}, {w.h[8], w.h[1]}};
            for (int e = 0; e < 2; ++e) {
                const int dev = ends[e][0] - 1, cmPin = ends[e][1];
                if (dev < 0 || dev >= placed.size() || !placed[dev].id) continue;
                const int pin = ourPin(*placed[dev].symbol, file.devices[dev], cmPin);
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
            // an end on a bus line (h[9]/h[10]: the wire it ends on); entry
            // names are encoded (negative) or plain numbers
            // names may be plain numbers, which only mean something on their
            // own bus: they are qualified with the bus name ("1.16")
            auto onBus = [&](int wire) { return wire > 0 && wire <= file.wires.size() && file.wires[wire - 1].isBus(); };
            auto entryName = [&](int v, int bus) {
                if (v < 0) return decodeName(v);
                const int b = file.wires[bus - 1].h[0];
                return (b < 0 ? decodeName(b) : QString::number(b)) + u'.' + QString::number(v);
            };
            if (w.h[7] == 0 && (w.h[0] < 0 || (w.h[0] > 0 && onBus(w.h[9]))))
                entries << Entry{out.points.first(), out.points.value(1), entryName(w.h[0], w.h[9]), w.h[2]};
            if (w.h[8] == 0 && (w.h[1] < 0 || (w.h[1] > 0 && onBus(w.h[10]))))
                entries << Entry{out.points.last(), out.points.value(out.points.size() - 2), entryName(w.h[1], w.h[10]), w.h[2]};
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

    // ---- CircuitMaker connects only where wires end; a wire that merely
    // runs over another net's pin tip gets a small step around it
    {
        struct Tip { QPointF at; int cmNet; };
        QVector<Tip> tips;
        for (int i = 0; i < placed.size(); ++i) {
            if (!placed[i].id) continue;
            const Device& d = file.devices[i];
            const Component* c = circuit.component(placed[i].id);
            for (int cmPin = 1; cmPin < d.nets.size(); ++cmPin)
                if (const int pin = ourPin(*placed[i].symbol, d, cmPin); pin >= 0)
                    tips << Tip{library.pinScenePos(*c, pin), d.nets[cmPin]};
        }
        auto blocked = [&](const QVector<QPointF>& path, int net) {
            for (int i = 0; i + 1 < path.size(); ++i)
                for (const Tip& t : std::as_const(tips))
                    if (t.cmNet != net && WireGeometry::onSegment(t.at, path[i], path[i + 1]))
                        return true;
            return false;
        };
        int stepped = 0;
        for (auto it = wireNetOf.cbegin(); it != wireNetOf.cend(); ++it) {
            Wire w = *circuit.wire(it.key());
            bool changed = false;
            for (int seg = 0; seg + 1 < w.points.size(); ++seg) {
                const QPointF a = w.points[seg], b = w.points[seg + 1];
                if ((a.x() != b.x() && a.y() != b.y()) || a == b) continue;
                const QPointF dir = (b - a) / QLineF(a, b).length();
                const QPointF across(-dir.y(), dir.x());
                // the crossed tip nearest to a; after stepping around it the
                // rest of the segment comes next
                const Tip* first = nullptr;
                for (const Tip& t : std::as_const(tips)) {
                    if (t.cmNet == it.value() || !WireGeometry::onSegment(t.at, a, b)) continue;
                    if (QLineF(a, t.at).length() < 10 || QLineF(b, t.at).length() < 10) continue;
                    if (!first || QLineF(a, t.at).length() < QLineF(a, first->at).length()) first = &t;
                }
                if (!first) continue;
                for (int side : {1, -1}) {
                    const QPointF p1 = first->at - dir * 10, p4 = first->at + dir * 10;
                    const QPointF p2 = p1 + across * 10 * side, p3 = p4 + across * 10 * side;
                    if (blocked({p1, p2, p3, p4}, it.value())) continue;
                    w.points.insert(seg + 1, p1);
                    w.points.insert(seg + 2, p2);
                    w.points.insert(seg + 3, p3);
                    w.points.insert(seg + 4, p4);
                    seg += 3;   // continue with p4 -> b
                    changed = true;
                    ++stepped;
                    break;
                }
            }
            if (changed) circuit.updateWire(w);
        }
        if (stepped)
            rep.warnings << QStringLiteral("%1 wires stepped around pins they crossed without connecting").arg(stepped);
    }

    // ---- rounding to our grid can lay a wire on top of another net's wire
    // (CircuitMaker allows wires one of its units apart): move such middle
    // segments sideways until they are clear
    {
        struct Seg { QPointF a, b; int net; };
        auto foreign = [&](ItemId self, int net) {
            QVector<Seg> out;
            for (auto it = wireNetOf.cbegin(); it != wireNetOf.cend(); ++it) {
                if (it.key() == self || it.value() == net) continue;
                const QVector<QPointF>& pts = circuit.wire(it.key())->points;
                for (int i = 0; i + 1 < pts.size(); ++i) out << Seg{pts[i], pts[i + 1], it.value()};
            }
            return out;
        };
        QVector<std::pair<QPointF, int>> tips;          // pin tips with their CM net
        for (int i = 0; i < placed.size(); ++i) {
            if (!placed[i].id) continue;
            const Device& d = file.devices[i];
            const Component* c = circuit.component(placed[i].id);
            for (int cmPin = 1; cmPin < d.nets.size(); ++cmPin)
                if (const int pin = ourPin(*placed[i].symbol, d, cmPin); pin >= 0)
                    tips.push_back({library.pinScenePos(*c, pin), d.nets[cmPin]});
        }
        auto touches = [&](const QVector<QPointF>& pts, int from, int to, const QVector<Seg>& others, int net) {
            for (int i = from; i < to; ++i) {
                for (const Seg& o : others)
                    if (WireGeometry::onSegment(o.a, pts[i], pts[i + 1]) || WireGeometry::onSegment(o.b, pts[i], pts[i + 1])
                        || WireGeometry::onSegment(pts[i], o.a, o.b) || WireGeometry::onSegment(pts[i + 1], o.a, o.b))
                        return true;
                for (const auto& [at, tipNet] : tips)
                    if (tipNet != net && WireGeometry::onSegment(at, pts[i], pts[i + 1])) return true;
            }
            return false;
        };
        int moved = 0;
        for (auto it = wireNetOf.cbegin(); it != wireNetOf.cend(); ++it) {
            Wire w = *circuit.wire(it.key());
            const QVector<Seg> others = foreign(it.key(), it.value());
            bool changed = false;
            for (int seg = 1; seg + 2 < w.points.size(); ++seg) {
                const QPointF a = w.points[seg], b = w.points[seg + 1];
                const bool horizontal = a.y() == b.y(), vertical = a.x() == b.x();
                if (horizontal == vertical) continue;
                if (!touches(w.points, seg, seg + 1, others, it.value())) continue;
                for (double off : {-10.0, 10.0, -20.0, 20.0}) {
                    QVector<QPointF> trial = w.points;
                    const QPointF d = horizontal ? QPointF(0, off) : QPointF(off, 0);
                    trial[seg] += d;
                    trial[seg + 1] += d;
                    if (touches(trial, seg - 1, seg + 2, others, it.value())) continue;
                    w.points = trial;
                    changed = true;
                    ++moved;
                    break;
                }
            }
            if (changed) circuit.updateWire(w);
        }
        if (moved)
            rep.warnings << QStringLiteral("%1 wire segments moved off wires of other nets").arg(moved);
    }

    // ---- connectivity check: every CM net must be one of our nets
    const Netlist nl = NetlistBuilder::build(circuit, library);
    struct Member { int ourNet; QPointF at; bool pin; };
    QMap<int, QVector<Member>> byCmNet;          // ordered: imports come out the same every time
    QHash<int, QString> netName;                // CM net -> terminal label on it
    QSet<int> throughContacts;                  // nets CircuitMaker joined across a closed contact
    for (int i = 0; i < placed.size(); ++i) {
        if (!placed[i].id) continue;
        const Device& d = file.devices[i];
        const SymbolDef& s = *placed[i].symbol;
        const Component* c = circuit.component(placed[i].id);
        // CircuitMaker saves the two sides of a closed contact (switch,
        // relay, window) as one net; only the wires tell those pins apart
        const QString kind = s.model.value(u"kind").toString();
        QHash<int, int> netUse;
        if (kind == u"link" || kind == u"relay")
            for (int cmPin = 1; cmPin < d.nets.size(); ++cmPin)
                if (ourPin(s, d, cmPin) >= 0 && d.nets[cmPin] > 0) netUse[d.nets[cmPin]]++;
        for (int cmPin = 1; cmPin < d.nets.size(); ++cmPin) {
            const int net = d.nets[cmPin];
            const int pin = ourPin(s, d, cmPin);
            if (net <= 0 || pin < 0) continue;
            if (netUse.value(net) > 1) {
                throughContacts << net;
                continue;
            }
            byCmNet[net] << Member{nl.netOf(c->id, pin), library.pinScenePos(*c, pin), true};
            if (s.netLabel && !c->label.isEmpty())
                netName.insert(net, c->label);
        }
    }
    for (auto it = wireNetOf.cbegin(); it != wireNetOf.cend(); ++it)
        byCmNet[it.value()] << Member{nl.wireNet.value(it.key(), -1), circuit.wire(it.key())->points.first(), false};

    // CircuitMaker numbers each side of a bus entry separately; entries with
    // the same name are one node
    QHash<int, int> cmNode;                     // CM net -> representative
    auto nodeOf = [&](int net) {
        while (cmNode.contains(net) && cmNode[net] != net) net = cmNode[net];
        return net;
    };
    QHash<QString, int> entryNet;
    for (const Entry& e : std::as_const(entries)) {
        if (e.cmNet <= 0) continue;
        if (auto it = entryNet.constFind(e.name); it != entryNet.cend())
            cmNode[nodeOf(e.cmNet)] = nodeOf(*it);
        else
            entryNet.insert(e.name, e.cmNet);
    }
    QHash<int, QSet<int>> cmNetsOfOurNet;
    for (auto it = byCmNet.cbegin(); it != byCmNet.cend(); ++it)
        for (const Member& m : it.value())
            if (m.ourNet >= 0) cmNetsOfOurNet[m.ourNet].insert(nodeOf(it.key()));
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
        if (throughContacts.contains(it.key())) continue;      // the wires decide
        QMap<int, QVector<QPointF>> groups;    // our net -> member points
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
                // later repairs must not cross it
                for (QPointF p : std::as_const(w.points))
                    spots << Spot{p, main};
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
    rep.shorts = shorts;
    if (shorts)
        rep.warnings << QStringLiteral("%1 places where redrawn symbols touch wires of another net; check them").arg(shorts);
    return true;
}

} // namespace

} // namespace cm::CktImporter
