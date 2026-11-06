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
//   device: name; type x y delay-1 pinMax; [v>4.011: extra]; nets[0..pinMax];
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
#include "core/io/CmDeviceLib.h"
#include "core/io/CmUserLib.h"
#include "core/WireGeometry.h"
#include "core/io/NativeFormat.h"
#include "library/LibraryRegistry.h"
#include "library/Macros.h"

#include <QFile>
#include <QFileInfo>
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
#include <optional>
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
    int position() const { return m_p; }

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
    d.delay = std::clamp(head[3] + 1, 1, 14);   // stored less one
    const int pinMax = head[4];
    if (modern) {
        r.integer();
        d.nets = r.ints(pinMax + 1);
        if (d.type == 0xc3) {
            const int n = r.integer();
            for (int k = 0; k < n; ++k) d.generator << r.token().toDouble();
        }
    } else {
        d.nets = r.ints(std::abs(pinMax) + 1);
        if (pinMax < 0)
            r.ints(20);
    }
    d.ref = r.string();
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
    d.spice = r.string();                       // SPICE template ("%D %1 0 DC %V"); empty: not simulated
    r.string();
    if (version > 4.0) {
        d.kind = r.string();                    // "type:digital", or a model alias with parameters
        d.package = r.string();
    }
    // package pin map: 0, the pins of this part, then the pins of every part
    const QVector<int> pkg = r.ints(r.integer() + 1);
    const int pins = std::abs(pinMax);
    if (pins > 0 && pkg.size() >= 1 + 2 * pins)
        d.packagePins = pkg.mid(1 + pins, pins);
    d.misc = r.ints(8);
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
    f.trailer = d.mid(r.position());
    return f;
}

// Colours of logic displays and cars, kept in nets[0] (Led.ckt, Cars.ckt).
QString cmColourName(int code)
{
    switch (code) {
    case 10: return QStringLiteral("red");
    case 11: return QStringLiteral("yellow");
    case 12: return QStringLiteral("green");
    case 13: return QStringLiteral("blue");
    case 14: return QStringLiteral("cyan");
    case 15: return QStringLiteral("magenta");
    default: return {};
    }
}

// The title block ends where the device list starts: the first line start
// from which a number and five strings lead exactly there.
std::optional<CktTitle> titleBefore(const QString& d, int listStart)
{
    static const QRegularExpression lineStart(QStringLiteral("(?m)^\\d+\\s*$"));
    for (auto it = lineStart.globalMatch(d.left(listStart)); it.hasNext();) {
        const auto m = it.next();
        try {
            Reader r(d, int(m.capturedStart()));
            CktTitle t;
            t.mode = r.integer();
            for (QString* s : {&t.title, &t.name, &t.date, &t.revision, &t.id})
                *s = r.string().trimmed();
            int end = r.position();
            while (end < listStart && d[end].isSpace()) ++end;
            if (end == listStart && t.mode >= 0 && t.mode <= 4) return t;
        } catch (const ParseError&) {
        }
    }
    return std::nullopt;
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
    QVector<CktFile::Probe> probes;
    {
        const QStringList lines = d.left(4000).split(u'\n');
        static const QRegularExpression count(QStringLiteral("^Probes:\\s*(\\d+)"));
        for (int i = 0; i < std::min<qsizetype>(lines.size(), 6); ++i)
            if (const auto m = count.match(lines[i].trimmed()); m.hasMatch()) {
                const int n = m.captured(1).toInt();
                for (int k = 0; k < n && i + 3 + 3 * k < lines.size(); ++k) {
                    const QStringList v = lines[i + 3 + 3 * k].split(u' ', Qt::SkipEmptyParts);
                    if (v.size() >= 3) probes << CktFile::Probe{lines[i + 1 + 3 * k].trimmed(), QPointF(v[1].toDouble(), v[2].toDouble())};
                }
                break;
            }
    }

    static const QRegularExpression candidate(QStringLiteral("(?m)^(\\d+)\\n(\\d+) "));
    QString lastError = QStringLiteral("device list not found");
    for (auto it = candidate.globalMatch(d); it.hasNext();) {
        const auto m = it.next();
        if (m.captured(1).toInt() <= 0) continue;
        try {
            CktFile f = parseAt(d, int(m.capturedStart()), version);
            f.probes = probes;
            f.title = titleBefore(d, int(m.capturedStart()));
            static const QRegularExpression options(QStringLiteral("(?m)^(\\d{6,}) -?\\d+\\s*$"));
            for (auto o = options.globalMatch(d.left(int(m.capturedStart()))); o.hasNext();)
                f.options = int(o.next().captured(1).toLongLong());
            static const QRegularExpression simLine(QStringLiteral("(?m)^0 0 -?\\d+ -?\\d+ -?\\d+(?=\\s*$)"));
            if (const auto s = simLine.match(d.left(int(m.capturedStart()))); s.hasMatch()) f.simLine = s.captured(0);
            return f;
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

// CircuitMaker's Device.lib, read once (empty when it is not installed).
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

// A switch model of the user's CircuitMaker Models folder (SW.MOD, CSW.MOD):
// ".MODEL SSW05 SW(VT=500.0m )" for the V->Switch labelled SW05. Returns
// the parameters in brackets, or nothing when not found.
std::optional<QString> cmSwitchModel(const QString& file, QChar prefix, const QString& name)
{
    static QHash<QString, QString> texts;
    if (!texts.contains(file)) {
        const QString dev = CmDeviceLib::locate(QSettings().value(QStringLiteral("cm/folder")).toString());
        QFile f(QFileInfo(dev).absolutePath() + QStringLiteral("/Models/") + file);
        texts.insert(file, f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()) : QString());
    }
    static const QRegularExpression line(QStringLiteral("^\\s*\\.MODEL\\s+(\\S+)\\s+\\w+\\s*\\(([^)]*)\\)"),
                                         QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    for (auto it = line.globalMatch(texts.value(file)); it.hasNext();) {
        const auto m = it.next();
        QString model = m.captured(1);
        if (model.endsWith(u'~')) model.chop(1);
        if (model.compare(prefix + name, Qt::CaseInsensitive) == 0) return m.captured(2).simplified();
    }
    return std::nullopt;
}

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
    // Pins whose inner net no device has reach the circuit through wires
    // that end on nothing (User.lib keeps no net on wires): Timer's CP, SI
    // and FI. Taken in order, top to bottom, when the counts agree.
    QSet<int> deviceNets;
    for (const Device& d : std::as_const(inner.devices))
        for (int n : d.nets) deviceNets << n;
    QVector<int> loosePins;
    for (int k = 0; k < m.pins.size(); ++k)
        if (m.pinNets.value(k) > 0 && !deviceNets.contains(m.pinNets.value(k))) loosePins << k;
    struct LooseEnd { int wire, end; QPointF at; };
    QVector<LooseEnd> looseEnds;
    for (int i = 0; i < inner.wires.size(); ++i) {
        const CktWire& w = inner.wires[i];
        if (w.isBus()) continue;
        for (int e : {0, 1}) {
            if (w.h[7 + e] != 0 || w.h[9 + e] != 0) continue;
            // an output's wire: its other end is already on a pin's net
            const int od = w.h[7 + (1 - e)] - 1, op = w.h[1 - e];
            if (od >= 0 && od < inner.devices.size() && m.pinNets.contains(inner.devices[od].nets.value(op, -1))) continue;
            const QPointF at = e == 0 ? w.points.first() : w.points.last();
            bool onOther = false;
            for (int j = 0; j < inner.wires.size() && !onOther; ++j)
                if (j != i && WireGeometry::onPolyline(at, inner.wires[j].points)) onOther = true;
            if (!onOther) looseEnds << LooseEnd{i, e, at};
        }
    }
    QHash<int, LooseEnd> looseOf;              // pin -> the wire end it takes
    if (!loosePins.isEmpty() && looseEnds.size() == loosePins.size()) {
        std::sort(looseEnds.begin(), looseEnds.end(), [](const LooseEnd& a, const LooseEnd& b) {
            return a.at.y() != b.at.y() ? a.at.y() < b.at.y() : a.at.x() < b.at.x();
        });
        for (int i = 0; i < loosePins.size(); ++i) looseOf.insert(loosePins[i], looseEnds[i]);
    }
    // A wire between two device pins connects them even where the devices'
    // own net numbers disagree: User.lib keeps stale numbers on some parts
    // (Timer's 74LS74 and inverters), and CircuitMaker follows the wires.
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
        if (auto it = looseOf.constFind(k); it != looseOf.cend()) {
            CktWire& w = inner.wires[it->wire];
            w.h[it->end] = 1;                       // the port's pin
            w.h[7 + it->end] = int(inner.devices.size());
        }
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
        if (d.delay > 1)
            c.props.insert(QStringLiteral("delay"), d.delay);
        // CircuitMaker leaves a part out of SPICE when its instance has an
        // empty template although the library device has one (5.x files)
        if (file.version >= 5.0 && d.spice.trimmed().isEmpty())
            if (const CmLibDevice* dev = deviceLib().find(d.name); dev && !dev->spice.trimmed().isEmpty())
                c.props.insert(QStringLiteral("noSpice"), true);
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
        // where CircuitMaker looks its model up ("N-EMOS 3T:A~", for a model
        // its .mod file sends on to another library): written back as it was
        if (!d.ref.isEmpty() && d.type != 0x5e) {
            c.props.insert(QStringLiteral("cmRef"), d.ref);
            // with the numbers that place its model ("546 256") and its package
            if (d.misc.size() == 8 && (d.misc[2] || d.misc[3]))
                c.props.insert(QStringLiteral("cmModelAt"), QStringLiteral("%1 %2").arg(d.misc[2]).arg(d.misc[3]));
            if (!d.package.isEmpty()) c.props.insert(QStringLiteral("cmPackage"), d.package);
        }
        // the instance's own flags where they are not the library's (which of
        // its quantities SPICE saves, 0x80 and 0x1000), apart from the shown texts
        if (const CmLibDevice* dev = deviceLib().find(d.name); dev && ((d.flags ^ dev->flags) & ~0x310))
            c.props.insert(QStringLiteral("cmFlags"), d.flags & ~0x300);
        // a single part's package pins when its model gives others than the
        // library ("pkg:TO-18 3,2,1"): CircuitMaker names SPICE nodes by them (Q1_3)
        if (const CmLibDevice* dev = deviceLib().find(d.name);
            dev && dev->parts <= 1 && !d.packagePins.isEmpty() && d.packagePins != dev->packageMap.mid(1, d.packagePins.size())) {
            QStringList pins;
            for (int p : d.packagePins) pins << QString::number(p);
            c.props.insert(QStringLiteral("cmPins"), pins.join(u' '));
        }
        // an instance's own SPICE template (741.ckt's BB: "I=I(VB)*10.61E6-..."), kept for export
        if (!d.spice.trimmed().isEmpty() && d.type != 0xc3)
            if (const CmLibDevice* dev = deviceLib().find(d.name); dev && dev->spice.trimmed() != d.spice.trimmed())
                c.props.insert(QStringLiteral("cmSpice"), d.spice);
        // the CircuitMaker device it was, when not the symbol's first one
        // (Op-Amp7 for an LM301A): written back as such
        if (!s->aliases.isEmpty() && cmName.compare(s->aliases.first(), Qt::CaseInsensitive) != 0)
            c.props.insert(QStringLiteral("cmDevice"), d.name);
        // relays named by their coil (SPDTRELA.SUB): pull-in voltage and coil resistance
        if (s->id == u"relay_spdt") {
            static const QHash<QString, std::pair<const char*, const char*>> relays{
                {QStringLiteral("5VSPDT"), {"4", "500"}}, {QStringLiteral("12VSPDT"), {"9.6", "1000"}},
                {QStringLiteral("24VSPDT"), {"19.2", "1500"}}, {QStringLiteral("120VSPDT"), {"96", "2000"}}};
            if (const auto r = relays.constFind(d.value.trimmed().toUpper()); r != relays.cend()) {
                c.props.insert(QStringLiteral("pullin"), QString::fromLatin1(r->first));
                c.props.insert(QStringLiteral("resistance"), QString::fromLatin1(r->second));
            }
        }
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
        // Ascii Display: the cursor in nets[10], then the 64 characters two
        // to a word (high byte first)
        if (kind == u"asciiterm" && d.nets.size() > 42) {
            QString text;
            for (int k = 11; k < 43; ++k)
                for (int ch : {(d.nets[k] >> 8) & 0xff, d.nets[k] & 0xff})
                    text += QChar(ch >= 32 && ch < 127 ? ch : ' ');
            c.props.insert(QStringLiteral("text"), text);
            c.props.insert(QStringLiteral("cursor"), std::clamp(d.nets[10], 0, 63));
        }
        // the position is nets[10]; "5V" alone only labels the switch
        // (Digfault and Display open with their "5V" switches off)
        if (kind == u"logicswitch")
            c.props.insert(QStringLiteral("state"), int(d.nets.size() > 10 && d.nets[10] == 1));
        else if (kind == u"pulser" && d.nets.size() > 8 && d.nets[7] > 0 && d.nets[8] > 0)
            c.props.insert(QStringLiteral("high"), d.nets[7]), c.props.insert(QStringLiteral("low"), d.nets[8]);
        else if (kind == u"car") {
            if (d.nets.size() > 14 && d.nets[14] > 0)
                c.props.insert(QStringLiteral("speed"), d.nets[14]);  // as CircuitMaker keeps it
            if (const QString col = cmColourName(d.nets.value(0)); !col.isEmpty()) c.props.insert(QStringLiteral("color"), col);
        } else if (s->id == u"logicdisplay") {
            if (const QString col = cmColourName(d.nets.value(0)); !col.isEmpty()) c.props.insert(QStringLiteral("color"), col);
        }
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
        } else if (s->id == u"vswitch" || s->id == u"iswitch") {
            // the switch model the label names, from CircuitMaker's own Models
            const bool v = s->id == u"vswitch";
            const QString name = d.value.trimmed();
            static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_]+$"));
            if (plain.match(name).hasMatch())
                if (const auto params = cmSwitchModel(v ? QStringLiteral("SW.MOD") : QStringLiteral("CSW.MOD"), v ? u'S' : u'W', name)) {
                    const QString model = (v ? QStringLiteral("VSW_") : QStringLiteral("ISW_")) + name.toUpper();
                    QString line = s->spice.value(u"line").toString();
                    line.replace(v ? QStringLiteral("VSW_GEN") : QStringLiteral("ISW_GEN"), model);
                    c.props.insert(QStringLiteral("spice"), line);
                    c.props.insert(QStringLiteral("spiceModels"),
                                   QStringLiteral(".model %1 %2(%3)").arg(model, v ? QStringLiteral("sw") : QStringLiteral("csw"), *params));
                }
        } else if (kind == u"siggen") {
            // "-1/1V": low/high levels
            static const QRegularExpression levels(QStringLiteral("^\\s*([-+]?[\\d.]+[a-zA-Z]*)\\s*/\\s*([-+]?[\\d.]+[a-zA-Z]*)"));
            if (const auto m = levels.match(d.value); m.hasMatch()) {
                const double lo = spiceValue(m.captured(1)), hi = spiceValue(m.captured(2));
                c.props.insert(QStringLiteral("amplitude"), QString::number((hi - lo) / 2));
                c.props.insert(QStringLiteral("offset"), QString::number((hi + lo) / 2));
            }
            c.label = s->defaultLabel;
            // the waveform as the instance's SPICE line gives it:
            // "%D %1 %2 DC 0 PULSE(0 5 100n 10n 10n 100n 1u) AC 1 0"
            static const QRegularExpression shape(QStringLiteral("\\b(SIN|PULSE|EXP|PWL|SFFM)\\s*\\(([^)]*)\\)"),
                                                  QRegularExpression::CaseInsensitiveOption);
            // CircuitMaker writes the SPICE line from the 20 numbers (the
            // line saved can be stale): nets[3] says the waveform, 1 pulse,
            // 2 sine, 3 FM, 5 piecewise linear; the first number is the AC
            // magnitude (0: none)
            const QVector<double>& g = d.generator;
            const int wave = d.nets.value(3);
            auto num = [&](int i) { return QString::number(g.value(i), 'g', 6); };
            if (g.size() >= 10 && (wave == 1 || wave == 2 || wave == 3 || wave == 5)) {
                if (wave == 2) {
                    c.props.insert(QStringLiteral("wave"), QStringLiteral("sine"));
                    c.props.insert(QStringLiteral("frequency"), num(1));
                    c.props.insert(QStringLiteral("offset"), num(2));
                    c.props.insert(QStringLiteral("amplitude"), num(3));
                } else if (wave == 1) {
                    c.props.insert(QStringLiteral("wave"), QStringLiteral("pulse"));
                    const char* keys[] = {"v1", "v2", "delay", "rise", "fall", "width", "period"};
                    for (int k = 0; k < 7; ++k) c.props.insert(QString::fromLatin1(keys[k]), num(2 + k));
                    c.props.insert(QStringLiteral("offset"), QStringLiteral("0"));
                } else if (wave == 3) {
                    c.props.insert(QStringLiteral("wave"), QStringLiteral("fm"));
                    c.props.insert(QStringLiteral("carrier"), num(1));
                    c.props.insert(QStringLiteral("offset"), num(2));
                    c.props.insert(QStringLiteral("amplitude"), num(3));
                    c.props.insert(QStringLiteral("mdi"), num(4));
                    c.props.insert(QStringLiteral("signal"), num(5));
                } else {
                    // pairs from the third number up to the end time in the second
                    QStringList pts;
                    for (int k = 2; k + 1 < g.size(); k += 2) {
                        pts << num(k) << num(k + 1);
                        if (g[k] >= g[1]) break;
                    }
                    c.props.insert(QStringLiteral("wave"), QStringLiteral("pwl"));
                    c.props.insert(QStringLiteral("pwl"), pts.join(u' '));
                    c.props.insert(QStringLiteral("offset"), QStringLiteral("0"));
                }
                c.props.insert(QStringLiteral("ac"), g[0] != 0);
                if (g[0] != 0) c.props.insert(QStringLiteral("acMag"), num(0));
            } else if (const auto m = shape.match(d.spice); m.hasMatch()) {
                const QString fn = m.captured(1).toUpper();
                const QStringList a = m.captured(2).split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
                auto put = [&](const char* key, int i) {
                    if (i < a.size()) c.props.insert(QString::fromLatin1(key), a[i]);
                };
                static const QRegularExpression dc(QStringLiteral("\\bDC\\s+(\\S+)"), QRegularExpression::CaseInsensitiveOption);
                if (const auto v = dc.match(d.spice); v.hasMatch() && fn != u"SIN" && fn != u"SFFM")
                    c.props.insert(QStringLiteral("offset"), v.captured(1));
                if (fn == u"SIN") {
                    c.props.insert(QStringLiteral("wave"), QStringLiteral("sine"));
                    put("offset", 0), put("amplitude", 1), put("frequency", 2), put("delay", 3), put("damping", 4);
                } else if (fn == u"PULSE") {
                    c.props.insert(QStringLiteral("wave"), QStringLiteral("pulse"));
                    put("v1", 0), put("v2", 1), put("delay", 2), put("rise", 3), put("fall", 4), put("width", 5), put("period", 6);
                } else if (fn == u"EXP") {
                    c.props.insert(QStringLiteral("wave"), QStringLiteral("exp"));
                    put("v1", 0), put("v2", 1), put("rd", 2), put("rt", 3), put("fd", 4), put("ft", 5);
                } else if (fn == u"PWL") {
                    c.props.insert(QStringLiteral("wave"), QStringLiteral("pwl"));
                    c.props.insert(QStringLiteral("pwl"), a.join(u' '));
                } else {                                        // SFFM
                    c.props.insert(QStringLiteral("wave"), QStringLiteral("fm"));
                    put("offset", 0), put("amplitude", 1), put("carrier", 2), put("mdi", 3), put("signal", 4);
                }
                static const QRegularExpression acPart(QStringLiteral("\\bAC\\s+(\\S+)(?:\\s+(\\S+))?"), QRegularExpression::CaseInsensitiveOption);
                if (const auto ac = acPart.match(d.spice.mid(m.capturedEnd())); ac.hasMatch()) {
                    c.props.insert(QStringLiteral("ac"), true);
                    c.props.insert(QStringLiteral("acMag"), ac.captured(1));
                    if (!ac.captured(2).isEmpty()) c.props.insert(QStringLiteral("acPhase"), ac.captured(2));
                } else {
                    c.props.insert(QStringLiteral("ac"), false);
                }
            }
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

    // the analysis setup after the texts (Simulation > Analyses Setup):
    // ".OPTIONS" line; enabled analyses (1 operating point, 4 DC sweep, 8 AC,
    // 16 transient) and four numbers; the DC sweep's source, start, stop,
    // step and a second source with its range; AC points, sweep type, start
    // and stop; transient start, stop, step and maximum step
    AnalysisSettings analysis;
    bool analysisRead = false;
    try {
        Reader r(file.trailer, 0);
        QString opts = r.string().simplified();
        if (opts.startsWith(QStringLiteral(".OPTIONS"), Qt::CaseInsensitive)) opts = opts.mid(8).trimmed();
        const int enabled = r.integer();
        r.ints(4);
        auto num = [&] {
            const double v = r.token().toDouble();
            return QString::number(v, 'g', 6);
        };
        const QString dcSource = r.string().trimmed();
        const QString dcStart = num(), dcStop = num(), dcStep = num();
        const QString dcSource2 = r.string().trimmed();
        const QString dcStart2 = num(), dcStop2 = num(), dcStep2 = num();
        const int acPoints = r.integer();
        r.integer();
        const QString acStart = num(), acStop = num();
        num();
        const QString tranStop = num(), tranStep = num();
        analysis.spiceOptions = opts;
        analysis.cmAnalyses = enabled;
        analysis.dcSource2 = dcSource2;
        analysis.dcStart2 = dcStart2, analysis.dcStop2 = dcStop2, analysis.dcStep2 = dcStep2;
        analysis.dcSource = dcSource;
        if (dcStep.toDouble() != 0) analysis.dcStart = dcStart, analysis.dcStop = dcStop, analysis.dcStep = dcStep;
        if (acStop.toDouble() > 0) {
            analysis.acStart = acStart;
            analysis.acStop = acStop;
            analysis.acPointsPerDecade = std::max(1, acPoints);
        }
        if (tranStop.toDouble() > 0) analysis.tranStop = tranStop, analysis.tranStep = tranStep;
        using T = AnalysisSettings::Type;
        analysis.type = enabled & 16 ? T::Transient : enabled & 8 ? T::AC : enabled & 4 ? T::DCSweep
                      : enabled & 1 ? T::OperatingPoint : T::Transient;
        analysisRead = true;
    } catch (const ParseError&) {
    }

    // circuits with parts that only make sense in SPICE open in analog mode
    int analogOnly = 0;
    for (const Placed& p : std::as_const(placed))
        if (p.symbol && !p.symbol->spice.isEmpty() && p.symbol->model.isEmpty())
            ++analogOnly;
    for (const Placed& p : std::as_const(placed))
        if (p.symbol && (p.symbol->model.value(u"kind").toString() == u"siggen"))
            ++analogOnly;
    // the file's own mode where it records one
    if (file.options) analogOnly = (*file.options & 0x80000) ? 1 : 0;
    if (analogOnly > 0 || analysisRead || file.options) {
        AnalysisSettings a = analysisRead ? analysis : AnalysisSettings{};
        a.analogMode = analogOnly > 0;
        if (file.options) a.cmOptions = *file.options;
        a.cmSimLine = file.simLine;
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
        // no wire ends on it: our pins where Device.lib puts CircuitMaker's
        // (as the exporter places a device with no wires)
        if (const CmLibDevice* dev = anchorDeltas[i].isEmpty() ? deviceLib().find(file.devices[i].name) : nullptr) {
            Component probe = placed[i].comp;
            probe.pos = {};
            const QTransform turn = LibraryRegistry::placement(probe);
            const QPointF origin(file.devices[i].x, file.devices[i].y);
            for (int k = 1; k <= std::min(dev->pinCount, int(dev->pins.size())); ++k) {
                const int pin = ourPin(*placed[i].symbol, file.devices[i], k);
                if (pin < 0) continue;
                const QPointF cmPin = (origin + turn.map(QPointF(dev->pins[k - 1]))) * kScale;
                anchorDeltas[i] << cmPin - turn.map(placed[i].symbol->pins[pin].pos);
            }
        }
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
    // name: as CircuitMaker shows it; key: with the bus, since an entry joins
    // only entries of the same name on buses of the same name (help: "Bus
    // Connection Wires")
    struct Entry {
        QPointF at, from;
        QString name, bus;
        int cmNet;
        ItemId wire = 0;
        bool first = true;
        QString key() const { return bus + u'\x1f' + name; }
    };
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
            auto onBus = [&](int wire) { return wire > 0 && wire <= file.wires.size() && file.wires[wire - 1].isBus(); };
            auto entryName = [&](int v) { return v < 0 ? decodeName(v) : QString::number(v); };
            auto busName = [&](int wire) {
                if (!onBus(wire)) return QString();
                const int b = file.wires[wire - 1].h[0];
                return b < 0 ? decodeName(b) : QString::number(b);
            };
            if (w.h[7] == 0 && (w.h[0] < 0 || (w.h[0] > 0 && onBus(w.h[9]))))
                entries << Entry{out.points.first(), out.points.value(1), entryName(w.h[0]), busName(w.h[9]), w.h[2], id, true};
            if (w.h[8] == 0 && (w.h[1] < 0 || (w.h[1] > 0 && onBus(w.h[10]))))
                entries << Entry{out.points.last(), out.points.value(out.points.size() - 2), entryName(w.h[1]), busName(w.h[10]),
                                 w.h[2], id, false};
        }
    }
    QHash<std::pair<qint64, qint64>, QString> entryAt;    // entry labels placed: point -> key
    auto keyOf = [](QPointF p) { return std::pair<qint64, qint64>{qRound64(p.x()), qRound64(p.y())}; };
    for (const Entry& e : std::as_const(entries)) {
        if (!netLabel || e.name.isEmpty()) continue;
        QPointF at = e.at;
        if (const auto taken = entryAt.constFind(keyOf(at)); taken != entryAt.cend()) {
            if (*taken == e.key()) continue;
            // entries of two names may end at one point of a bus in
            // CircuitMaker; here wire ends that meet connect, so this one
            // slides along the bus to a free point
            const Wire* bus = nullptr;
            for (const Wire& w : circuit.wires())
                if (w.bus && WireGeometry::onPolyline(at, w.points)) { bus = &w; break; }
            const Wire* wire = circuit.wire(e.wire);
            if (!bus || !wire) continue;
            QPointF dir;
            for (int i = 0; i + 1 < bus->points.size() && dir.isNull(); ++i)
                if (WireGeometry::onSegment(at, bus->points[i], bus->points[i + 1])) {
                    const QPointF d = bus->points[i + 1] - bus->points[i];
                    if (const double len = std::hypot(d.x(), d.y()); len > 0) dir = d / len;
                }
            std::optional<QPointF> free;
            for (int step = 1; step <= 6 && !free && !dir.isNull(); ++step)
                for (const int sign : {1, -1}) {
                    const QPointF p = at + dir * (10.0 * step * sign);
                    if (!WireGeometry::onPolyline(p, bus->points) || entryAt.contains(keyOf(p))) continue;
                    const bool touches = std::any_of(circuit.wires().cbegin(), circuit.wires().cend(), [&](const Wire& w) {
                        return !w.bus && w.id != e.wire && WireGeometry::onPolyline(p, w.points);
                    });
                    if (!touches) { free = p; break; }
                }
            if (!free) continue;
            Wire moved = *wire;
            WireGeometry::moveEnd(moved.points, e.first ? 0 : int(moved.points.size()) - 1, *free - at);
            moved.points = WireGeometry::simplify(moved.points);
            circuit.updateWire(moved);
            at = *free;
        }
        entryAt.insert(keyOf(at), e.key());
        Component label;
        label.symbolId = netLabel->id;
        label.pos = at;
        label.label = e.name;
        if (!e.bus.isEmpty())
            label.props.insert(QStringLiteral("bus"), e.bus);
        label.props.insert(QStringLiteral("side"), entrySide(e.at, e.from));
        circuit.addComponent(label);
    }

    if (file.title && (file.title->mode || !file.title->title.isEmpty() || !file.title->name.isEmpty())) {
        // CircuitMaker keeps it in a page corner; here it goes below the drawing
        TitleBlock tb;
        tb.title = file.title->title;
        tb.author = file.title->name;
        tb.date = file.title->date;
        tb.revision = file.title->revision;
        // CircuitMaker shows it on printed pages, not on the screen
        tb.visible = false;
        double right = 0, bottom = 0;
        for (const Component& c : circuit.components())
            right = std::max(right, c.pos.x()), bottom = std::max(bottom, c.pos.y());
        tb.pos = QPointF(right, bottom + 100);
        circuit.setTitleBlock(tb);
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
        // relay, window, the key held in a key matrix) as one net; only the
        // wires tell those pins apart
        const QString kind = s.model.value(u"kind").toString();
        QHash<int, int> netUse;
        // (a resistor is no contact: a ResistorWire's ends are one net, and joined)
        const bool contact = (kind == u"link" && s.model.value(u"type").toString() != u"resistor") || kind == u"relay"
                             || kind == u"keymatrix" || kind == u"contact";
        if (contact)
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
        if (auto it = entryNet.constFind(e.key()); it != entryNet.cend())
            cmNode[nodeOf(e.cmNet)] = nodeOf(*it);
        else
            entryNet.insert(e.key(), e.cmNet);
    }
    // a CircuitMaker net of wires alone (a stray wire drawn over another)
    // connects nothing, so joining it to a net is no short
    QSet<int> withPins;
    for (auto it = byCmNet.cbegin(); it != byCmNet.cend(); ++it)
        for (const Member& m : it.value())
            if (m.pin) withPins.insert(nodeOf(it.key()));
    QHash<int, QSet<int>> cmNetsOfOurNet;
    for (auto it = byCmNet.cbegin(); it != byCmNet.cend(); ++it)
        for (const Member& m : it.value())
            if (m.ourNet >= 0 && withPins.contains(nodeOf(it.key()))) cmNetsOfOurNet[m.ourNet].insert(nodeOf(it.key()));
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

    // every ground symbol is one node, in CircuitMaker as in SPICE: groups
    // that each reach a ground are joined already
    QSet<int> groundNets;
    for (const Component& c : circuit.components())
        if (c.symbolId == u"ground")
            groundNets << nl2.netOf(c.id, 0);

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
            if (groundNets.contains(g.key()) && groundNets.contains(main)) continue;
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
    // waveform probes, on the nearest wire of the redrawn circuit
    if (!file.probes.isEmpty()) {
        QVector<QPointF> points;
        for (const CktFile::Probe& p : file.probes) {
            const QPointF want = scaled(p.at);
            QPointF best = want;
            double bestDist = 40;
            for (const Wire& w : circuit.wires()) {
                if (w.bus) continue;
                for (int k = 0; k + 1 < w.points.size(); ++k) {
                    const QPointF a = w.points[k], b = w.points[k + 1];
                    const QPointF ab = b - a;
                    const double len2 = QPointF::dotProduct(ab, ab);
                    const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(want - a, ab) / len2, 0.0, 1.0) : 0;
                    const QPointF q = a + ab * t;
                    if (const double dd = QLineF(q, want).length(); dd < bestDist) bestDist = dd, best = q;
                }
            }
            points << best;
        }
        circuit.setProbes(points);
    }
    if (qEnvironmentVariableIsSet("CM_TRAILER"))
        qInfo("TRAILER %s", qPrintable(file.trailer.simplified().left(700)));
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
