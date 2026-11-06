#include "core/io/CktExporter.h"

#include "core/NetlistBuilder.h"
#include "core/WireGeometry.h"
#include "core/io/CmDeviceLib.h"
#include "core/io/CmUserLib.h"
#include "library/LibraryRegistry.h"
#include "library/Macros.h"

#include <QSettings>

#include <QDate>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

namespace cm::CktExporter {

namespace {

// our 10-unit grid onto CircuitMaker's 3-unit one (the importer's inverse)
constexpr double kToCm = 0.3;
constexpr int kCharWidth = 7;           // CircuitMaker's label rectangles: 7 units a character, 8 high

QPoint toCm(QPointF p) { return {int(std::lround(p.x() * kToCm)), int(std::lround(p.y() * kToCm))}; }

// CircuitMaker's colour codes for logic displays and cars (nets[0]); red
// for the colours it has none of
int cmColourCode(const QString& name)
{
    static const QHash<QString, int> codes{{QStringLiteral("yellow"), 11}, {QStringLiteral("green"), 12}, {QStringLiteral("blue"), 13},
                                           {QStringLiteral("cyan"), 14}, {QStringLiteral("magenta"), 15}};
    return codes.value(name, 10);
}

// "<length> <text>"; a newline is CR LF and counts two
QString str(QString s)
{
    s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    s.replace(u'\n', QStringLiteral("\r\n"));
    return s.isEmpty() ? QStringLiteral("0") : QString::number(s.size()) + u' ' + s;
}

QString join(const QVector<int>& v)
{
    QStringList parts;
    for (int x : v) parts << QString::number(x);
    return parts.join(u' ');
}

QString rect(const QRect& r) { return join({r.left(), r.top(), r.right(), r.bottom()}); }

// A bus or bus-entry name as CircuitMaker stores it: the characters read as
// base-64 digits (0-9, A-Z, a-z, _) minus 64^length; up to 5 characters.
std::optional<int> encodeName(const QString& name)
{
    if (name.isEmpty() || name.size() > 5) return std::nullopt;
    qint64 code = 0, base = 1;
    for (QChar c : name) {
        int d;
        if (c >= u'0' && c <= u'9') d = c.unicode() - '0';
        else if (c >= u'A' && c <= u'Z') d = c.unicode() - 'A' + 10;
        else if (c >= u'a' && c <= u'z') d = c.unicode() - 'a' + 36;
        else if (c == u'_') d = 62;
        else return std::nullopt;
        code = code * 64 + d;
        base *= 64;
    }
    return int(code - base);
}

// A bus name: plain numbers are stored as they are.
std::optional<int> busCode(const QString& name)
{
    static const QRegularExpression digits(QStringLiteral("^[1-9]\\d{0,8}$"));
    if (digits.match(name).hasMatch()) return name.toInt();
    return encodeName(name);
}

// The lower median: the importer takes the upper median of the opposite
// differences, so an even number of votes comes back to the same place.
int median(QVector<double> v)
{
    std::sort(v.begin(), v.end());
    return int(std::lround(v[(v.size() - 1) / 2]));
}

struct Device {
    const Component* c = nullptr;
    const SymbolDef* s = nullptr;
    const CmLibDevice* lib = nullptr;
    int part = 1;                   // 1-based, in a multi-part package
    QVector<int> ourPin;            // our pin index of CircuitMaker pin 1, 2, ...
    QPoint origin;
    QTransform turn;                // rotation and mirror, no translation
    QPoint pinAt(int cmPin) const { return origin + turn.map(lib->pins[cmPin - 1]); }
};

// package pin number of CircuitMaker pin k (1-based) of a part
int packageNumber(const Device& d, int k)
{
    const CmLibDevice& l = *d.lib;
    // the instance's own pins where the file gave them (its model's package)
    const QString key = l.parts > 1 ? QStringLiteral("cmPackageTable") : QStringLiteral("cmPins");
    if (const QStringList kept = d.c->props.value(key).toString().split(u' ', Qt::SkipEmptyParts); !kept.isEmpty()) {
        const int at = l.parts > 1 ? l.pinCount * (d.part - 1) + k - 1 : k - 1;
        if (at < kept.size()) return kept[at].toInt();
    }
    const QVector<int>& map = l.packageMap;
    if (l.parts > 1) return map.value(1 + l.pinCount * d.part + k - 1);
    return map.value(k);
}

QVector<int> pinMapping(const Device& d)
{
    const SymbolDef& s = *d.s;
    const int n = d.lib->pinCount;
    QVector<int> out(n, -1);
    bool numbered = false;
    for (int i = 0; i < s.pins.size() && !numbered; ++i) numbered = !packagePin(s, *d.c, i).isEmpty();
    for (int k = 1; k <= n; ++k) {
        if (!s.cmPins.isEmpty()) {
            out[k - 1] = s.pinIndex(s.cmPins.value(k - 1));
        } else if (numbered) {
            const QString number = QString::number(packageNumber(d, k));
            for (int i = 0; i < s.pins.size(); ++i)
                if (packagePin(s, *d.c, i) == number) out[k - 1] = i;
        } else if (k - 1 < s.pins.size()) {
            out[k - 1] = k - 1;
        }
    }
    return out;
}

int cmAngle(const Component& c)
{
    static const int angles[4] = {0, 270, 180, 90};     // CircuitMaker turns counter-clockwise
    // it mirrors after turning: a mirrored quarter turn is the opposite one
    const int r = ((c.rotation % 4) + 4) % 4;
    return angles[c.mirrored && r % 2 ? (r + 2) % 4 : r] | (c.mirrored ? 0x200 : 0);
}

QString hexBytes(const QVector<int>& bytes)
{
    QStringList lines;
    QString line;
    for (int b : bytes) {
        line += QChar(u'A' + ((b >> 4) & 15));
        line += QChar(u'A' + (b & 15));
        if (line.size() >= 64) lines << line, line.clear();
    }
    if (!line.isEmpty()) lines << line;
    return lines.join(u'\n');
}

QVector<int> dataBytes(const Component& c)
{
    QVector<int> out;
    for (const QString& t : c.props.value(QStringLiteral("data")).toString().split(u' ', Qt::SkipEmptyParts))
        out << t.toInt(nullptr, 16);
    return out;
}

// "4.7k", "1Meg" -> number; 0 when unreadable
double number(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("^\\s*([-+]?[\\d.]+(?:[eE][-+]?\\d+)?)\\s*([a-zA-Z]*)"));
    const auto m = re.match(text);
    if (!m.hasMatch()) return 0;
    const double v = m.captured(1).toDouble();
    const QString suffix = m.captured(2).toLower();
    if (suffix.startsWith(u"meg")) return v * 1e6;
    static const QHash<QChar, double> scale{{u'f', 1e-15}, {u'p', 1e-12}, {u'n', 1e-9}, {u'u', 1e-6},
                                            {u'm', 1e-3}, {u'k', 1e3}, {u'g', 1e9}, {u't', 1e12}};
    return suffix.isEmpty() ? v : v * scale.value(suffix[0], 1);
}

qint32 floatBits(double v)
{
    const float f = float(v);
    qint32 bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// Days since 30 Dec 1899, as CircuitMaker stamps its devices.
int oleDate() { return int(QDate(1899, 12, 30).daysTo(QDate::currentDate())); }

// A device's two stamps, the date it was placed and its place in the order
// of placing, are 32-bit floats that CircuitMaker writes and reads as the low
// half of a double ("5.89008e-315 5.26354e-315" is 36608.3 and 1.0). The
// order decides which part names a SPICE node (the earliest: "Vcc_1"), and
// expressions such as the 741's V(vcc_1) depend on it.
QString floatStamp(double v)
{
    const quint64 bits = quint32(floatBits(v));
    double d;
    std::memcpy(&d, &bits, 8);
    return v == 0 ? QStringLiteral("0") : QString::number(d, 'g', 17);
}

const char* const kHeaderStart =
    "CircuitMaker Text\n5.6\nProbes: 0\n"
    "41 35 62 4 97 108 86 46 82 16 73 113 113 59 105 107 \n"
    "0 5 0 1e+09 0.001 2\n33\n"
    "13 10 13 12 18 10 20 20 12 20 \n10 13 13 10 20 13 46 20 14 20 \n"
    "18 17 14 16 20 20 20 20 10 13 \n20 18 11 ";

// After the text list: the analysis settings, all left at CircuitMaker's defaults.
// After the text list: the analysis setup (see CktImporter.cpp), then
// settings left at CircuitMaker's defaults.
QString trailer(const AnalysisSettings& a)
{
    using T = AnalysisSettings::Type;
    auto num = [](const QString& v) { return QString::number(number(v), 'g', 6); };
    const int bit = a.type == T::OperatingPoint ? 1 : a.type == T::DCSweep ? 4 : a.type == T::AC ? 8
                  : a.type == T::Transient || a.type == T::Fourier ? 16 : 0;
    // the analyses the file had while ours is among them (17: operating
    // point and transient), else ours with its other settings
    const int had = a.cmAnalyses;
    const int enabled = had >= 0 && (had & bit) ? had : (had >= 0 ? had & ~0x1d : 2048) | bit;
    // CircuitMaker ends the .OPTIONS line with its own CR LF, inside the string
    QStringList l{a.spiceOptions.trimmed().isEmpty() ? QStringLiteral("0") : str(QStringLiteral(".OPTIONS ") + a.spiceOptions.trimmed() + u'\n'),
                  QStringLiteral("%1 0 0").arg(enabled), QStringLiteral("0"), QStringLiteral("0"),
                  str(a.dcSource), QStringList{num(a.dcStart), num(a.dcStop), num(a.dcStep)}.join(u' '),
                  str(a.dcSource2), QStringList{num(a.dcStart2), num(a.dcStop2), num(a.dcStep2)}.join(u' '),
                  QStringLiteral("%1 %2 %3 %4").arg(a.acPointsPerDecade).arg(a.acSweep == u"lin" ? 0 : a.acSweep == u"oct" ? 2 : 1)
                      .arg(num(a.acStart), num(a.acStop)),
                  QStringLiteral("%1 %2 %3 %4").arg(num(a.tranStart.isEmpty() ? QStringLiteral("0") : a.tranStart), num(a.tranStop), num(a.tranStep),
                                                    num(a.tranMax.isEmpty() ? a.tranStep : a.tranMax))};
    const char* const general[] = {"0", "0", "0 0 0", "0 0 0", "0", "0", "0 0 0 0", "0", "0 0 0 0 0", "0 0 0 0", "0",
                                   "0 0 0", "0", "0 0 0", "0 0 0 0 0 0 0 0 0"};
    for (const char* s : general) l << QString::fromLatin1(s);
    const char* const block[] = {"0 0 0 0 0 0", "0 0 0 0", "0 0 0 0", "0 0", "0 0", "0 0", "0 0", "0 0",
                                 "0 0 0 0 0 0", "0 0", "0 0 0", "0"};
    for (int k = 0; k < 8; ++k)
        for (const char* s : block) l << QString::fromLatin1(s);
    return l.join(u'\n');
}

struct WireOut {
    QVector<QPoint> pts;            // CircuitMaker units
    int pin[2] = {0, 0}, dev[2] = {0, 0}, onWire[2] = {0, 0};
    int net = -1;                   // ours
};

bool onSegment(QPoint p, QPoint a, QPoint b)
{
    const qint64 cross = qint64(b.x() - a.x()) * (p.y() - a.y()) - qint64(b.y() - a.y()) * (p.x() - a.x());
    return cross == 0 && p.x() >= std::min(a.x(), b.x()) && p.x() <= std::max(a.x(), b.x())
           && p.y() >= std::min(a.y(), b.y()) && p.y() <= std::max(a.y(), b.y());
}

bool onPolyline(QPoint p, const QVector<QPoint>& pts)
{
    for (int k = 0; k + 1 < pts.size(); ++k)
        if (onSegment(p, pts[k], pts[k + 1])) return true;
    return false;
}

// CircuitMaker's pins are not always where ours are: a wire ending on its
// pin, or routed along our narrower symbol, can run over the tip of another
// pin. CircuitMaker does not connect it but draws it so; move such segments
// a grid step aside (a stub keeps an end on its pin).
void clearPinTips(QVector<WireOut>& outs, const QVector<Device>& devices, const Netlist& netlist)
{
    struct Tip { QPoint at; int net; };
    QVector<Tip> tips;
    for (const Device& d : devices)
        for (int k = 1; k <= d.lib->pinCount; ++k)
            tips << Tip{d.pinAt(k), d.ourPin[k - 1] < 0 ? -1 : netlist.netOf(d.c->id, d.ourPin[k - 1])};
    auto crossesTip = [&](const QVector<QPoint>& pts, int net) {
        for (int k = 0; k + 1 < pts.size(); ++k)
            for (const Tip& t : std::as_const(tips))
                if ((t.net != net || t.net < 0) && onSegment(t.at, pts[k], pts[k + 1])) return true;
        return false;
    };
    for (int i = 0; i < outs.size(); ++i) {
        WireOut& o = outs[i];
        if (!crossesTip(o.pts, o.net)) continue;
        // ends of other wires: those on this one must stay on it, those of
        // other nets must not come to lie on it
        QVector<QPoint> held, foreign;
        for (int j = 0; j < outs.size(); ++j) {
            if (j == i) continue;
            for (QPoint end : {outs[j].pts.first(), outs[j].pts.last()}) {
                if (outs[j].net == o.net && onPolyline(end, o.pts)) held << end;
                else if (outs[j].net != o.net) foreign << end;
            }
        }
        for (int k = 0; k + 1 < o.pts.size(); ++k) {
            QVector<QPoint> one{o.pts[k], o.pts[k + 1]};
            if (!crossesTip(one, o.net)) continue;
            const bool horizontal = one[0].y() == one[1].y();
            if (!horizontal && one[0].x() != one[1].x()) continue;
            for (int off : {3, -3, 6, -6}) {
                QVector<QPoint> trial = o.pts;
                int a = k;
                if (a == 0) { trial.prepend(trial.first()); ++a; }
                if (a + 1 == trial.size() - 1) trial.append(trial.last());
                const QPoint d = horizontal ? QPoint(0, off) : QPoint(off, 0);
                trial[a] += d;
                trial[a + 1] += d;
                if (crossesTip(trial, o.net)) continue;
                if (std::any_of(held.cbegin(), held.cend(), [&](QPoint p) { return !onPolyline(p, trial); })) continue;
                if (std::any_of(foreign.cbegin(), foreign.cend(), [&](QPoint p) { return onPolyline(p, trial); })) continue;
                o.pts = trial;
                k = a;
                break;
            }
        }
    }
}

} // namespace

const CmLibDevice* deviceFor(const SymbolDef& s, const CmDeviceLib& lib)
{
    for (const QString& a : s.aliases) {
        if (const CmLibDevice* d = lib.find(a + u'~')) return d;
        if (const CmLibDevice* d = lib.find(a)) return d;
    }
    for (const QString& a : s.aliases)
        if (const CmLibDevice* d = lib.findByValue(a)) return d;
    // net labels: CircuitMaker joins terminals of the same name
    if (s.netLabel) return lib.find(QStringLiteral("Terminal~"));
    return nullptr;
}

// The macro headers of CircuitMaker's User.lib (records like Device.lib's),
// read once; a macro instance in a .ckt is a device record with its name.
const CmDeviceLib& userMacros()
{
    static const CmDeviceLib lib = [] {
        CmDeviceLib l;
        const QString path = CmUserLib::locate(QSettings().value(QStringLiteral("cm/folder")).toString());
        if (!path.isEmpty()) l.load(path);
        return l;
    }();
    return lib;
}

// A macro of User.lib as its instances are written: only the pins in use
// (the header has room for 64), the package map that many zeros.
const CmLibDevice* macroFor(const SymbolDef& s)
{
    if (!macros::isMacro(s)) return nullptr;
    static QHash<QString, CmLibDevice> instances;
    for (const QString& name : {s.name, s.name + u'~'}) {
        if (const auto it = instances.constFind(name); it != instances.cend()) return &*it;
        const CmLibDevice* d = userMacros().find(name);
        if (!d) continue;
        CmLibDevice m = *d;
        m.pinCount = std::min(int(s.cmPins.size()), int(m.pins.size()));
        m.pins.resize(m.pinCount);
        m.packageMap = QVector<int>(1 + 2 * m.pinCount, 0);
        m.parts = 0;
        return &*instances.insert(name, m);
    }
    return nullptr;
}

QByteArray write(const Circuit& circuit, const LibraryRegistry& library, const CmDeviceLib& lib, CktExportReport* report)
{
    // macros CircuitMaker has (User.lib) are written by name; if any other
    // is used, the circuit goes out with every macro laid out as its contents
    for (const Component& c : circuit.components())
        if (const SymbolDef* s = library.find(c.symbolId); s && macros::isMacro(*s) && !macroFor(*s)) {
            if (std::unique_ptr<Circuit> flat = macros::flatten(circuit, library)) {
                // the instances go; the net labels on their pins stay and join
                // the contents (CircuitMaker terminals connect by name)
                QList<ItemId> instances;
                for (const Component& m : flat->components())
                    if (const SymbolDef* ms = library.find(m.symbolId); ms && macros::isMacro(*ms)) instances << m.id;
                for (ItemId id : instances) flat->removeComponent(id);
                CktExportReport inner;
                const QByteArray data = write(*flat, library, lib, &inner);
                inner.macrosExpanded = true;
                if (report) *report = inner;
                return data;
            }
            break;
        }
    CktExportReport rep;
    const Netlist netlist = NetlistBuilder::build(circuit, library);
    auto netNumber = [&netlist](int net) -> int {
        if (net < 0 || net >= netlist.nets.size()) return 0;
        const Net& n = netlist.nets[net];
        return n.pins.size() + n.wires.size() > 1 ? net + 1 : 0;
    };

    // ---- bus lines, and the net labels on them: their entries
    QVector<const Wire*> buses;
    for (const Wire& w : circuit.wires()) {
        if (!w.bus || w.points.size() < 2) continue;
        if (busCode(w.name)) buses << &w;
        else rep.skipped << QStringLiteral("bus %1").arg(w.name);
    }
    struct BusEntry { QPointF at; int bus; int code; };
    QVector<BusEntry> entries;
    QSet<ItemId> entryLabels;
    for (const Component& c : circuit.components()) {
        const SymbolDef* s = library.find(c.symbolId);
        if (!s || !s->netLabel || s->pins.isEmpty()) continue;
        const QPointF at = library.pinScenePos(c, 0);
        // an entry is where a wire ends on the bus; a label merely lying on
        // one is a terminal
        const bool wireEnds = std::any_of(circuit.wires().cbegin(), circuit.wires().cend(), [&](const Wire& w) {
            return !w.bus && !w.points.isEmpty()
                   && (WireGeometry::near(w.points.first(), at) || WireGeometry::near(w.points.last(), at));
        });
        for (int b = 0; wireEnds && b < buses.size(); ++b) {
            if (!WireGeometry::onPolyline(at, buses[b]->points)) continue;
            // an entry's name, on its bus; older files numbered them "DATA.3"
            const QString prefix = buses[b]->name + u'.';
            const bool scoped = !c.props.value(QStringLiteral("bus")).toString().isEmpty();
            const QString number = scoped ? c.label : c.label.startsWith(prefix) ? c.label.mid(prefix.size()) : QString();
            bool numeric = false;
            const int n = number.toInt(&numeric);
            const std::optional<int> code = numeric && n >= 0 ? std::optional<int>(n) : encodeName(c.label);
            // a second label at an entry stays a terminal, tied to it below
            if (!code || std::any_of(entries.cbegin(), entries.cend(), [&](const BusEntry& e) { return e.at == at; })) break;
            entries << BusEntry{at, b, *code};
            entryLabels << c.id;
            break;
        }
    }

    // ---- devices
    QVector<Device> devices;
    for (const Component& c : circuit.components()) {
        const SymbolDef* s = library.find(c.symbolId);
        if (!s || entryLabels.contains(c.id)) continue;
        Device d;
        d.c = &c;
        d.s = s;
        d.lib = deviceFor(*s, lib);
        // the device an imported part was, if it is still one of this symbol's
        if (const QString was = c.props.value(QStringLiteral("cmDevice")).toString(); !was.isEmpty()) {
            QString bare = was;
            if (bare.endsWith(u'~')) bare.chop(1);
            if (s->aliases.contains(bare, Qt::CaseInsensitive))
                if (const CmLibDevice* dev = lib.find(was)) d.lib = dev;
        }
        if (!d.lib) d.lib = macroFor(*s);
        if (!d.lib) {
            rep.skipped << shownDesignator(*s, c) + QStringLiteral(" (") + s->name + u')';
            continue;
        }
        d.part = packagePart(*s, c);
        // a part drawn on its own (one flip-flop of a 74112) keeps its letter
        // in the designator: U1B is the second part of U1
        if (s->parts.isEmpty() && d.lib->parts > 1 && c.designator.size() > 1 && c.designator.back().isLetter()
            && c.designator.at(c.designator.size() - 2).isDigit())
            d.part = std::clamp(c.designator.back().toUpper().unicode() - u'A' + 1, 1, d.lib->parts);
        if (d.lib->parts <= 1 || d.lib->packageMap.size() < 1 + d.lib->pinCount * (d.part + 1)) d.part = 1;
        d.ourPin = pinMapping(d);
        Component turned = c;
        turned.pos = {};
        d.turn = LibraryRegistry::placement(turned);
        // the origin that puts CircuitMaker's pins where ours are; like the
        // importer, from the pins wires end on, one vote per wire end, so a
        // symbol of another width comes back where it was
        QVector<double> xs, ys, allX, allY;
        for (int k = 1; k <= d.lib->pinCount; ++k) {
            if (d.ourPin[k - 1] < 0) continue;
            const QPointF pin = library.pinScenePos(c, d.ourPin[k - 1]);
            const QPointF at = pin * kToCm - d.turn.map(QPointF(d.lib->pins[k - 1]));
            allX << at.x();
            allY << at.y();
            for (const Wire& w : circuit.wires()) {
                if (w.bus || w.points.isEmpty()) continue;
                const int ends = WireGeometry::near(w.points.first(), pin) + WireGeometry::near(w.points.last(), pin);
                for (int n = 0; n < ends; ++n) { xs << at.x(); ys << at.y(); }
            }
        }
        if (xs.isEmpty()) { xs = allX; ys = allY; }
        d.origin = xs.isEmpty() ? toCm(c.pos) : QPoint(median(xs), median(ys));
        devices << d;
    }

    QHash<QString, int> packageIds;
    QStringList records;
    for (int i = 0; i < devices.size(); ++i) {
        const Device& d = devices[i];
        const Component& c = *d.c;
        const SymbolDef& s = *d.s;
        const CmLibDevice& l = *d.lib;
        const QString kind = s.model.value(u"kind").toString();
        const QString name = l.name;

        QVector<int> nets{0};
        for (int k = 1; k <= l.pinCount; ++k)
            nets << (d.ourPin[k - 1] < 0 ? 0 : netNumber(netlist.netOf(c.id, d.ourPin[k - 1])));
        QString value = c.label.isEmpty() ? l.value : c.label;
        // a macro's Input / Output: its name in nets[10..17], eight characters
        // padded with spaces (CircuitMaker makes them the subcircuit's ports)
        if (s.id == u"port_in" || s.id == u"port_out") {
            nets.resize(10);
            const QString portName = c.label.left(8).leftJustified(8, u' ');
            for (QChar ch : portName) nets << (ch.unicode() < 128 ? ch.unicode() : '?');
        }
        // device state kept in the net list after the pins
        if (kind == u"logicswitch") {
            // the position is nets[10]; the value text only labels it
            const bool on = c.props.value(QStringLiteral("state")).toInt();
            value = on ? QStringLiteral("5V") : QStringLiteral("0V");
            if (on) {
                nets.resize(10);
                nets << 1;
            }
        } else if (kind == u"pulser") {
            nets << 0 << 0 << c.props.value(QStringLiteral("high"), 10).toInt() << c.props.value(QStringLiteral("low"), 10).toInt()
                 << 0 << 0;
            value.clear();
        } else if (name == u"Hex Key~") {
            const int v = std::clamp(c.props.value(QStringLiteral("value")).toInt(), 0, 15);
            nets.resize(10);                            // the value in nets[10], its character in nets[11]
            nets << v << (v < 10 ? '0' + v : 'A' + v - 10);
            value.clear();
        } else if (s.id == u"logicdisplay") {
            nets[0] = cmColourCode(c.props.value(QStringLiteral("color")).toString());
        } else if (kind == u"car") {
            // colour in nets[0], speed in nets[14], as CircuitMaker keeps them
            nets[0] = cmColourCode(c.props.value(QStringLiteral("color")).toString());
            nets.resize(21);
            nets[14] = std::max(1, c.props.value(QStringLiteral("speed"), 3).toInt());
        } else if (name == u"Ascii Key~") {
            nets.resize(9);
            nets << 0 << 0 << std::clamp(c.props.value(QStringLiteral("value"), 65).toInt(), 32, 126);
            value.clear();
        } else if (name == u"Ascii Display~") {
            // the cursor, then the 64 characters two to a word
            nets.resize(10);
            nets << std::clamp(c.props.value(QStringLiteral("cursor")).toInt(), 0, 63);
            const QString text = c.props.value(QStringLiteral("text")).toString().leftJustified(64, u' ', true);
            for (int k = 0; k < 32; ++k) {
                auto code = [&](int i) { const int u = text.at(i).unicode(); return u >= 32 && u < 127 ? u : ' '; };
                nets << (code(2 * k) << 8 | code(2 * k + 1));
            }
            value = l.value;
        } else if (name == u"Hex Display~") {
            nets[0] = 10;
            nets << 0 << 0 << 0 << 0 << 0 << 0 << 1 << 1 << 1 << 1 << 1 << 1;
            value = l.value;
        } else if (name == u"Logic Display~") {
            nets[0] = 10;
            value = l.value;
        }
        // signal generator (0xC3): sine parameters in the nets and in a block of
        // its own; CircuitMaker writes the SPICE line from them
        // an empty template leaves the part out of CircuitMaker's simulation
        QString spice = c.props.value(QStringLiteral("noSpice")).toBool() ? QString()
                      : c.props.value(QStringLiteral("cmSpice"), l.spice).toString();
        QStringList genBlock;                   // the generator's 20 numbers
        if (l.type == 0xc3) {
            auto prop = [&](const char* key, const char* fallback) {
                return number(c.props.value(QString::fromLatin1(key), QString::fromLatin1(fallback)).toString());
            };
            auto g = [](double v) { return QString::number(v, 'g', 6); };
            const QString wave = c.props.value(QStringLiteral("wave"), QStringLiteral("sine")).toString();
            // the AC magnitude first (0: no AC stimulus)
            const double ac = c.props.value(QStringLiteral("ac"), true).toBool() ? prop("acMag", "1") : 0;
            nets.resize(3);
            QVector<double> params;
            int pairs = 0;
            if (wave == u"pulse") {
                // PULSE(v1 v2 delay rise fall width period): nets[3] = 1
                const double v1 = prop("v1", "0"), v2 = prop("v2", "5"), period = std::max(prop("period", "1m"), 1e-12);
                params = {ac, 1 / period, v1, v2, prop("delay", "0"), prop("rise", "1n"), prop("fall", "1n"), prop("width", "0.5m"), period};
                nets << 1;
                value = QStringLiteral("%1/%2V").arg(g(v1), g(v2));
                // (the template's own %D %1 %2 stay as they are)
                spice = QStringLiteral("%D %1 %2 DC 0 PULSE(")
                      + QStringList{g(v1), g(v2), g(params[4]), g(params[5]), g(params[6]), g(params[7]), g(period)}.join(u' ') + u')';
            } else if (wave == u"fm") {
                // SFFM(offset amplitude carrier mdi signal): nets[3] = 3
                const double off = prop("offset", "0"), amp = prop("amplitude", "1");
                params = {ac, prop("carrier", "10k"), off, amp, prop("mdi", "1"), prop("signal", "1k")};
                nets << 3;
                value = QStringLiteral("%1/%2V").arg(g(off - amp), g(off + amp));
                spice = QStringLiteral("%D %1 %2 DC 0 SFFM(") + QStringList{g(off), g(amp), g(params[1]), g(params[4]), g(params[5])}.join(u' ') + u')';
            } else if (wave == u"pwl") {
                // PWL(t v ...): nets[3] = 5; the end time, then the pairs (nine at most)
                QVector<double> pts;
                for (const QString& t : c.props.value(QStringLiteral("pwl")).toString().split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts))
                    pts << number(t);
                if (pts.size() % 2) pts.removeLast();
                if (pts.size() > 18) pts.resize(18);
                if (pts.isEmpty()) pts = {0, 0};
                pairs = int(pts.size()) / 2;
                params = {ac, pts[pts.size() - 2]};
                params << pts;
                nets << 5;
                double lo = pts[1], hi = pts[1];
                for (int k = 1; k < pts.size(); k += 2) lo = std::min(lo, pts[k]), hi = std::max(hi, pts[k]);
                value = QStringLiteral("%1/%2V").arg(g(lo), g(hi));
                QStringList txt;
                for (double v : std::as_const(pts)) txt << g(v);
                spice = QStringLiteral("%D %1 %2 DC 0 PWL( ") + txt.join(u' ') + u')';
            } else {
                // SIN(offset amplitude frequency): nets[3] = 2
                const double amplitude = prop("amplitude", "1"), offset = prop("offset", "0"), frequency = prop("frequency", "1k");
                params = {ac, frequency, offset, amplitude};
                nets << 2;
                value = QStringLiteral("%1/%2V").arg(g(offset - amplitude), g(offset + amplitude));
                spice = QStringLiteral("%D %1 %2 DC 0 SIN(") + QStringList{g(offset), g(amplitude), g(frequency)}.join(u' ')
                      + QStringLiteral(" 0 0)") + (ac != 0 ? QStringLiteral(" AC %1 0").arg(g(ac)) : QString());
            }
            // the AC stimulus on every waveform (Bandpass: "PULSE(...) AC 1 0")
            if (wave != u"sine" && wave != u"sin" && ac != 0 && !spice.contains(QStringLiteral(" AC ")))
                spice += QStringLiteral(" AC %1 0").arg(g(ac));
            // nets[4..6]: 'V' and the display range; nets[7]: pairs + 1 of a PWL
            nets << 'V' << (wave == u"pulse" || wave == u"pwl" ? -10 : -8) << (wave == u"pulse" || wave == u"pwl" ? 10 : 8)
                 << (pairs ? pairs + 1 : 0) << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0;
            for (double v : std::as_const(params)) nets << floatBits(v);
            if (wave == u"fm" || wave == u"pwl") nets.resize(65);       // as CircuitMaker keeps them
            for (double v : std::as_const(params)) genBlock << g(v);
            while (genBlock.size() < 20) genBlock << QStringLiteral("0");
        }
        // model parameters after the alias, as CircuitMaker keeps them
        QString modelKind = l.kind;
        if (s.id == u"pll") {
            const QVariantMap p = c.props;
            modelKind += QStringLiteral(" {Fc=%1 Fr=%2 Rs=%3}")
                             .arg(p.value(QStringLiteral("fc"), QStringLiteral("10k")).toString(),
                                  p.value(QStringLiteral("fr"), QStringLiteral("5k")).toString(),
                                  p.value(QStringLiteral("rs"), QStringLiteral("100")).toString());
        }
        if (s.id == u"ftov") {
            const QVariantMap p = c.props;
            modelKind = QStringLiteral("alias:XFTOV {VIL=%1 VIH=%2 CYCLES=%3}")
                            .arg(p.value(QStringLiteral("vil"), QStringLiteral("1")).toString(),
                                 p.value(QStringLiteral("vih"), QStringLiteral("2")).toString(),
                                 p.value(QStringLiteral("cycles"), QStringLiteral("1k")).toString());
        }
        // the parameters the file gave the alias ("alias:XTRANS {RATIO=0.1}"),
        // with the part's present values
        if (const QString kept = c.props.value(QStringLiteral("cmKind")).toString(); kept.contains(u'{')) {
            const qsizetype open = kept.indexOf(u'{'), close = kept.indexOf(u'}', open);
            QStringList pairs;
            for (const QString& pair : kept.mid(open + 1, close - open - 1).split(u' ', Qt::SkipEmptyParts)) {
                const qsizetype eq = pair.indexOf(u'=');
                QString value = eq > 0 ? pair.mid(eq + 1) : QString();
                for (auto it = c.props.cbegin(); eq > 0 && it != c.props.cend(); ++it)
                    if (s.defaultProps.contains(it.key()) && it.key().compare(pair.left(eq), Qt::CaseInsensitive) == 0)
                        value = it.value().toString();
                pairs << (eq > 0 ? pair.left(eq) + u'=' + value : pair);
            }
            modelKind = kept.left(open) + u'{' + pairs.join(u' ') + u'}' + (close >= 0 ? kept.mid(close + 1) : QString());
        }
        QVector<int> data;
        if (l.type == 0x50) {                           // PROM: 32 bytes after the record
            data = dataBytes(c);
            data.resize(32);
        } else if (l.type == 0xaa) {                    // data sequencer: address registers, then the bytes
            const QVector<int> bytes = dataBytes(c);
            const int lines = std::max(1, c.props.value(QStringLiteral("lines"), 32).toInt());
            nets.resize(11);
            nets << c.props.value(QStringLiteral("start")).toInt() + 1 << c.props.value(QStringLiteral("start")).toInt() + 1
                 << c.props.value(QStringLiteral("stop"), lines - 1).toInt() + 1 << c.props.value(QStringLiteral("tick"), 1).toInt()
                 << 1 << c.props.value(QStringLiteral("external")).toInt() << lines + 1;
            data << 0;
            for (int k = 0; k < lines; ++k) data << bytes.value(k);
        }

        // package map: this part's pins, then the whole table
        QVector<int> pkg{0};
        const int n = l.pinCount;
        if (l.parts > 1) {
            QVector<int> table = l.packageMap.mid(1 + n, n * l.parts);
            if (const QStringList kept = c.props.value(QStringLiteral("cmPackageTable")).toString().split(u' ', Qt::SkipEmptyParts);
                kept.size() == table.size()) {
                for (int k = 0; k < kept.size(); ++k) table[k] = kept[k].toInt();
            }
            pkg << table.mid(n * (d.part - 1), n) << table << 0;
        } else {
            QVector<int> own = l.packageMap.mid(1, n);
            own.resize(n);
            const QStringList kept = c.props.value(QStringLiteral("cmPins")).toString().split(u' ', Qt::SkipEmptyParts);
            // as the file had them, however many (Scr's generator: 2 of 4;
            // CircuitMaker fails on a longer map for its SIP2 package)
            if (!kept.isEmpty()) {
                own.clear();
                for (const QString& k : kept) own << k.toInt();
            }
            pkg << own << own << 0;
        }
        const int pinMax = int(nets.size()) - 1;

        // text: what is shown, and where (rectangles relative to the origin)
        const QString kindText = s.model.value(u"kind").toString();
        const QString designator = shownDesignator(s, c);
        const bool showDesignator = c.showDesignator.value_or(!s.netLabel && kindText != u"const") && !designator.isEmpty();
        const bool showLabel = c.showLabel.value_or(s.netLabel || s.prefix != u"U") && !value.isEmpty();
        Component turned = c;
        turned.pos = {};
        const QRectF r = LibraryRegistry::placement(turned).mapRect(s.bounds);
        auto textRect = [&](const QString& text, std::optional<QPointF> centre, QPointF leftAnchor) {
            const int w = kCharWidth * int(text.size());
            QPoint at;
            if (centre) {
                at = toCm(c.pos + *centre) - d.origin - QPoint(w / 2, 0);
            } else {
                at = toCm(c.pos + leftAnchor) - d.origin;
            }
            return QRect(QPoint(at.x(), at.y() - 4), QPoint(at.x() + w, at.y() + 4));
        };
        const QPointF designatorAnchor(r.right() + 3, r.top() + 6);
        const QPointF labelAnchor = s.netLabel ? QPointF(r.right() + 2, r.center().y())
                                               : QPointF(r.right() + 3, r.top() + (showDesignator ? 26 : 6));
        const int ownFlags = c.props.value(QStringLiteral("cmFlags"), l.flags).toInt();
        const int flags = (ownFlags & ~0x300) | 0x10 | (showDesignator ? 0x200 : 0) | (showLabel ? 0x100 : 0);

        int group = 0;
        if (l.parts > 1) {
            // the package: U3 for U3A and U3B drawn as separate parts
            QString package = c.designator;
            if (s.parts.isEmpty() && package.size() > 1 && package.back().isLetter() && package.at(package.size() - 2).isDigit())
                package.chop(1);
            const auto it = packageIds.constFind(package);
            group = it != packageIds.cend() ? *it : int(packageIds.size()) + 1;
            packageIds.insert(package, group);
        }

        QStringList rec;
        rec << str(name);
        // the fourth number is the propagation delay less one (Edit > Set Prop Delays)
        const int delay = std::clamp(c.props.value(QStringLiteral("delay"), 1).toInt(), 1, 14);
        rec << join({l.type, d.origin.x(), d.origin.y(), delay - 1, pinMax, std::max(int(pkg.size()) - 1, pinMax + 1)});
        rec << join(nets);
        if (l.type == 0xc3) rec << QString::number(genBlock.size()) << genBlock.join(u' ');
        // a macro instance names its User.lib macro here, and has a 1 first
        const bool macro = l.type == 0x5e;
        const QString ref = c.props.value(QStringLiteral("cmRef")).toString();
        rec << (macro ? str(name) : ref.isEmpty() ? QStringLiteral("0") : str(ref));
        rec << join({macro ? 1 : 0, 0, flags, cmAngle(c)});
        rec << str(value);
        if (!value.isEmpty()) rec << rect(textRect(value, c.labelAt, labelAnchor));
        rec << str(designator);
        if (!designator.isEmpty()) rec << rect(textRect(designator, c.designatorAt, designatorAnchor));
        // third label: the relay of a coil or contact
        const QString tag = s.defaultProps.contains(QStringLiteral("relay"))
                                ? c.props.value(QStringLiteral("relay"), s.defaultProps.value(QStringLiteral("relay"))).toString()
                                : QString();
        rec << str(tag);
        if (!tag.isEmpty()) rec << rect(textRect(tag, std::nullopt, QPointF(r.right() + 3, r.top() + 46)));
        rec << str(c.props.value(QStringLiteral("cmPower"), l.power).toString()) << str(spice) << QStringLiteral("0") << str(modelKind)
            << str(c.props.value(QStringLiteral("cmPackage"), l.package).toString());
        rec << QString::number(pkg.size() - 1) << join(pkg);
        const QStringList modelAt = c.props.value(QStringLiteral("cmModelAt")).toString().split(u' ', Qt::SkipEmptyParts);
        rec << join({l.code & 0xffff, 0, modelAt.value(0).toInt(), modelAt.value(1).toInt(), l.parts, l.parts > 1 ? d.part : 0, group, 0});
        rec << str(l.prefix);
        rec << join({i + 1, 0, 0});
        // the stamps as read; a file of 5.5 or older has none, and 5.6 needs
        // them (CircuitMaker fails opening Scr.ckt written without), so it gets ours
        if (const QStringList st = c.props.value(QStringLiteral("cmStamp")).toString().split(u' ', Qt::SkipEmptyParts); st.size() == 2) {
            rec << QStringLiteral("2") << st.join(u' ') << QStringLiteral("0");
        } else {
            // parts without a designator (grounds) last: CircuitMaker fails
            // when one of them is the earliest on a net (Math1.ckt)
            const int order = designator.isEmpty() ? int(devices.size()) + i : i;
            rec << QStringLiteral("2") << floatStamp(oleDate()) + u' ' + floatStamp(order) << QStringLiteral("0");
        }
        if (!data.isEmpty()) rec << hexBytes(data);
        records << rec.join(u'\n');
    }
    rep.devices = int(devices.size());

    // ---- wires: each end on a device pin, a bus line or another wire; the
    // bus lines come first, so wire j + 1 of the file is buses[j]
    QVector<const Wire*> wires;
    QVector<ItemId> wireIds;
    for (auto it = circuit.wires().cbegin(); it != circuit.wires().cend(); ++it) {
        if (it->bus || it->points.size() < 2) continue;
        wires << &*it;
        wireIds << it.key();
    }
    const int nb = int(buses.size());
    QStringList wireRecords;
    for (const Wire* b : std::as_const(buses)) {
        QStringList rec;
        rec << join({*busCode(b->name), 0, 1, 0, 0, 0x20, 0, 0, 0, 0, 0, int(b->points.size())});
        for (QPointF p : b->points) rec << join({toCm(p).x(), toCm(p).y()});
        wireRecords << rec.join(u'\n');
    }
    QVector<WireOut> outs;
    for (int i = 0; i < wires.size(); ++i) {
        const Wire& w = *wires[i];
        WireOut o;
        QVector<QPoint>& pts = o.pts;
        int (&pin)[2] = o.pin, (&dev)[2] = o.dev, (&onWire)[2] = o.onWire;
        for (QPointF p : w.points) pts << toCm(p);
        for (int e = 0; e < 2; ++e) {
            const QPointF end = e == 0 ? w.points.first() : w.points.last();
            bool found = false;
            for (const BusEntry& be : std::as_const(entries)) {
                if (found || !WireGeometry::near(be.at, end)) continue;
                found = true;
                pin[e] = be.code;
                onWire[e] = be.bus + 1;
            }
            for (int j = 0; j < devices.size() && !found; ++j) {
                const Device& d = devices[j];
                for (int k = 1; k <= d.lib->pinCount && !found; ++k) {
                    if (d.ourPin[k - 1] < 0 || !WireGeometry::near(library.pinScenePos(*d.c, d.ourPin[k - 1]), end)) continue;
                    found = true;
                    pin[e] = k;
                    dev[e] = j + 1;
                    // end exactly on CircuitMaker's pin: slide the last segment
                    // sideways when our symbol's pin is elsewhere, else add a jog
                    const QPoint exact = d.pinAt(k);
                    const int tip = e == 0 ? 0 : int(pts.size()) - 1, next = e == 0 ? 1 : tip - 1;
                    if (pts[tip] == exact) continue;
                    const bool slides = pts.size() > 2 && (pts[tip].y() == pts[next].y() || pts[tip].x() == pts[next].x());
                    if (slides) {
                        if (pts[tip].y() == pts[next].y()) pts[next].setY(exact.y());
                        else pts[next].setX(exact.x());
                        pts[tip] = exact;
                    } else if (e == 0) {
                        pts.prepend(exact);
                    } else {
                        pts.append(exact);
                    }
                }
            }
            // CircuitMaker joins an end to the one wire it names: a wire the
            // end lies along, else an earlier wire ending there (two ends
            // naming each other would make an island of the pair)
            for (int pass = 0; pass < 2 && !found; ++pass)
                for (int j = 0; j < wires.size() && !found; ++j) {
                    if (j == i || !WireGeometry::onPolyline(end, wires[j]->points)) continue;
                    const bool atEnd = WireGeometry::near(wires[j]->points.first(), end) || WireGeometry::near(wires[j]->points.last(), end);
                    if (pass == 0 ? atEnd : j > i) continue;
                    found = true;
                    onWire[e] = nb + j + 1;
                }
        }
        pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
        if (pts.size() == 1) pts << pts.first();
        o.net = netlist.wireNet.value(wireIds[i], -1);
        outs << o;
    }
    // terminals at a bus entry: a zero-length wire from the terminal to the entry
    for (int j = 0; j < devices.size(); ++j) {
        if (!devices[j].s->netLabel || devices[j].ourPin.isEmpty() || devices[j].ourPin[0] < 0) continue;
        const QPointF at = library.pinScenePos(*devices[j].c, devices[j].ourPin[0]);
        for (const BusEntry& be : std::as_const(entries)) {
            if (!WireGeometry::near(be.at, at)) continue;
            WireOut o;
            o.pts = {devices[j].pinAt(1), devices[j].pinAt(1)};
            o.pin[0] = 1;
            o.dev[0] = j + 1;
            o.pin[1] = be.code;
            o.onWire[1] = be.bus + 1;
            o.net = netlist.netOf(devices[j].c->id, devices[j].ourPin[0]);
            outs << o;
            break;
        }
    }
    clearPinTips(outs, devices, netlist);
    for (const WireOut& o : std::as_const(outs)) {
        QStringList rec;
        rec << join({o.pin[0], o.pin[1], netNumber(o.net), 0, 0, 0, 0, o.dev[0], o.dev[1], o.onWire[0], o.onWire[1],
                     int(o.pts.size())});
        for (QPoint p : o.pts) rec << join({p.x(), p.y()});
        wireRecords << rec.join(u'\n');
    }
    rep.wires = int(wireRecords.size());

    // ---- free text
    QStringList textRecords;
    for (const TextNote& t : circuit.texts()) {
        const int height = std::max(8, int(std::lround(t.pointSize / 2)));
        const QStringList lines = t.text.split(u'\n');
        int longest = 0;
        for (const QString& l : lines) longest = std::max(longest, int(l.size()));
        QPoint at = toCm(t.pos);
        QSize size(std::max(1, longest * height / 2) + 6, int(lines.size()) * height + 6);
        // a node name sits with its bottom edge on its wire
        if (t.nameAt) {
            const QPoint on = toCm(t.pos + *t.nameAt);
            size.setHeight(std::max(4, on.y() - at.y() + 1));
        }
        const QRect box(at, size);
        // "<colour> 0 0 <length>": CircuitMaker draws only <length> characters
        textRecords << join({height, height / 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 54}) + u'\n' + str(QStringLiteral("Courier New"))
                           + QStringLiteral("\n0 %1 0 %2\n").arg(t.nameAt ? 0x8000 : 0).arg(int(t.text.size() + lines.size() - 1)) + rect(box) + u'\n' + rect(t.nameAt ? box : box.adjusted(3, 2, -3, -2)) + u'\n'
                           + str(t.text);
    }

    // ---- header: the view over everything placed
    QRect extent;
    for (const Device& d : devices) extent |= QRect(d.origin - QPoint(40, 40), QSize(80, 80));
    if (extent.isEmpty()) extent = QRect(0, 0, 600, 300);
    QString head = QString::fromLatin1(kHeaderStart);
    // probes that were SAVE- parts go back as such, at their point; the
    // header lists each probe once per analysis it had (else for the transient)
    static const int colours[] = {65280, 65535, 16776960, 16711935, 11184640, 255, 16711680, 32768};
    QStringList probeEntries;
    int count = 0;
    for (int k = 0; k < circuit.probes().size(); ++k) {
        const QPointF pt = circuit.probes()[k];
        const Circuit::ProbeTag* tag = circuit.probeTag(pt);
        const QPoint at = tag && tag->cmAt ? *tag->cmAt : toCm(pt);
        if (tag && !tag->cmRecord.isEmpty()) {
            QStringList lines = tag->cmRecord.split(u'\n');
            QStringList h = lines.value(1).split(u' ', Qt::SkipEmptyParts);
            if (h.size() == 6) {
                h[1] = QString::number(at.x()), h[2] = QString::number(at.y());
                lines[1] = h.join(u' ');
            }
            records << lines.join(u'\n');
        }
        if (tag && !tag->cmHeader.isEmpty()) {
            for (const QString& e : tag->cmHeader) {
                const QStringList f = e.split(u'|');
                if (f.size() < 4) continue;
                probeEntries << f[0] << f[1] << QStringLiteral("%1 %2 %3 %4").arg(f[2]).arg(at.x()).arg(at.y()).arg(f[3]);
                ++count;
            }
            continue;
        }
        if (tag && !tag->cmRecord.isEmpty()) continue;
        // its own name, else its net's as CircuitMaker names it (GCM_2)
        QString name = circuit.probeName(pt);
        for (const Wire& w : circuit.wires())
            if (name.isEmpty() && !w.bus && WireGeometry::onPolyline(pt, w.points))
                name = NetlistBuilder::nodeName(netlist, netlist.wireNet.value(w.id, -1), circuit, library);
        probeEntries << (name.isEmpty() ? QStringLiteral("P%1").arg(k + 1) : name) << QStringLiteral("Transient Analysis")
                << QStringLiteral("%1 %2 %3 %4").arg(k).arg(at.x()).arg(at.y()).arg(colours[k % 8]);
        ++count;
    }
    if (count) {
        // "Probes: n", then name, analysis and "index x y colour" each
        QStringList p{QStringLiteral("Probes: %1").arg(count)};
        p << probeEntries;
        head.replace(QStringLiteral("Probes: 0"), p.join(u'\n'));
    }
    QStringList out{head};
    const AnalysisSettings& as = circuit.analysis();
    // "0 0 <speed> 100 <format>": the last number is the file format (10 for
    // 5.6, 9 for 5.5, 3 for 5.0); one kept from an older file says 10 here, or
    // CircuitMaker reads the rest as the older format and opens nothing (Diffamp)
    QStringList simLine = (as.cmSimLine.isEmpty() ? QStringLiteral("0 0 1 100 10") : as.cmSimLine).split(u' ', Qt::SkipEmptyParts);
    if (simLine.size() == 5) simLine[4] = QStringLiteral("10");
    out << simLine.join(u' ') << rect(extent);
    out << str(QStringLiteral("5.000 V")) << str(QStringLiteral("5.000 V")) << str(QStringLiteral("GND"));
    out << QStringLiteral("0 0") << QStringLiteral("24 100 0 0 0") << str(QStringLiteral("Package,Description,"))
        << QStringLiteral("0") << QStringLiteral("0 7") << QStringLiteral("0 4 0.500000 0.500000") << rect(extent)
        << QStringLiteral("%1 0").arg(((as.cmOptions >= 0 ? as.cmOptions : 42991634) & ~0x80000) | (as.analogMode ? 0x80000 : 0));
    const TitleBlock& tb = circuit.titleBlock();
    // title block: display mode (2: on all pages), title, name, date, revision, ID
    const bool titled = tb.visible || !tb.title.isEmpty() || !tb.author.isEmpty() || !tb.date.isEmpty() || !tb.revision.isEmpty();
    out << QString::number(titled ? 2 : 0) << str(tb.title) << str(tb.author) << str(tb.date) << str(tb.revision)
        << QStringLiteral("0");
    out << QString::number(records.size());
    out << records;
    out << QString::number(wireRecords.size());
    out << wireRecords;
    out << QString::number(textRecords.size());
    out << textRecords;
    out << trailer(circuit.analysis());
    QString text = out.join(u'\n') + u'\n';
    // lines end in CR LF; a newline inside a string (CR LF) becomes CR CR LF
    text.replace(u'\n', QStringLiteral("\r\n"));
    if (report) *report = rep;
    return text.toLatin1();
}

bool save(const QString& path, const Circuit& circuit, const LibraryRegistry& library, const CmDeviceLib& lib,
          CktExportReport* report, QString* error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(write(circuit, library, lib, report));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace cm::CktExporter
