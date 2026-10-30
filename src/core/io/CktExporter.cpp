#include "core/io/CktExporter.h"

#include "core/NetlistBuilder.h"
#include "core/WireGeometry.h"
#include "core/io/CmDeviceLib.h"
#include "library/LibraryRegistry.h"

#include <QDate>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cm::CktExporter {

namespace {

// our 10-unit grid onto CircuitMaker's 3-unit one (the importer's inverse)
constexpr double kToCm = 0.3;
constexpr int kCharWidth = 7;           // CircuitMaker's label rectangles: 7 units a character, 8 high

QPoint toCm(QPointF p) { return {int(std::lround(p.x() * kToCm)), int(std::lround(p.y() * kToCm))}; }

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

int median(QVector<double> v)
{
    std::sort(v.begin(), v.end());
    return int(std::lround(v[v.size() / 2]));
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
    return angles[((c.rotation % 4) + 4) % 4] | (c.mirrored ? 0x200 : 0);
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

const char* const kHeaderStart =
    "CircuitMaker Text\n5.6\nProbes: 0\n"
    "41 35 62 4 97 108 86 46 82 16 73 113 113 59 105 107 \n"
    "0 5 0 1e+09 0.001 2\n33\n"
    "13 10 13 12 18 10 20 20 12 20 \n10 13 13 10 20 13 46 20 14 20 \n"
    "18 17 14 16 20 20 20 20 10 13 \n20 18 11 ";

// After the text list: the analysis settings, all left at CircuitMaker's defaults.
QString trailer()
{
    QStringList l{QStringLiteral("0"), QStringLiteral("2048 0 0")};
    const char* const general[] = {"0", "0", "0", "0 0 0", "0", "0 0 0", "0 0 0 0", "0 0 0 0", "0", "0", "0 0 0",
                                   "0 0 0", "0", "0", "0 0 0 0", "0", "0 0 0 0 0", "0 0 0 0", "0", "0 0 0", "0",
                                   "0 0 0", "0 0 0 0 0 0 0 0 0"};
    for (const char* s : general) l << QString::fromLatin1(s);
    const char* const block[] = {"0 0 0 0 0 0", "0 0 0 0", "0 0 0 0", "0 0", "0 0", "0 0", "0 0", "0 0",
                                 "0 0 0 0 0 0", "0 0", "0 0 0", "0"};
    for (int k = 0; k < 8; ++k)
        for (const char* s : block) l << QString::fromLatin1(s);
    return l.join(u'\n');
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

QByteArray write(const Circuit& circuit, const LibraryRegistry& library, const CmDeviceLib& lib, CktExportReport* report)
{
    CktExportReport rep;
    const Netlist netlist = NetlistBuilder::build(circuit, library);
    auto netNumber = [&netlist](int net) -> int {
        if (net < 0 || net >= netlist.nets.size()) return 0;
        const Net& n = netlist.nets[net];
        return n.pins.size() + n.wires.size() > 1 ? net + 1 : 0;
    };

    // ---- devices
    QVector<Device> devices;
    for (const Component& c : circuit.components()) {
        const SymbolDef* s = library.find(c.symbolId);
        if (!s) continue;
        Device d;
        d.c = &c;
        d.s = s;
        d.lib = deviceFor(*s, lib);
        if (!d.lib) {
            rep.skipped << shownDesignator(*s, c) + QStringLiteral(" (") + s->name + u')';
            continue;
        }
        d.part = packagePart(*s, c);
        if (d.lib->parts <= 1 || d.lib->packageMap.size() < 1 + d.lib->pinCount * (d.part + 1)) d.part = 1;
        d.ourPin = pinMapping(d);
        Component turned = c;
        turned.pos = {};
        d.turn = LibraryRegistry::placement(turned);
        QVector<double> xs, ys;
        for (int k = 1; k <= d.lib->pinCount; ++k) {
            if (d.ourPin[k - 1] < 0) continue;
            const QPointF at = library.pinScenePos(c, d.ourPin[k - 1]) * kToCm - d.turn.map(QPointF(d.lib->pins[k - 1]));
            xs << at.x();
            ys << at.y();
        }
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
            nets << 0 << 0 << 0 << 0 << 0 << v << (v < 10 ? '0' + v : 'A' + v - 10);
            value.clear();
        } else if (name == u"Ascii Key~") {
            nets.resize(9);
            nets << 0 << 0 << std::clamp(c.props.value(QStringLiteral("value"), 65).toInt(), 32, 126);
            value.clear();
        } else if (name == u"Ascii Display~") {
            nets.resize(9);
            nets << 0 << 0;
            for (int k = 0; k < 32; ++k) nets << 0x2020;
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
        QString spice = l.spice;
        QVector<int> sine;
        if (l.type == 0xc3) {
            const double amplitude = number(c.props.value(QStringLiteral("amplitude"), QStringLiteral("1")).toString());
            const double offset = number(c.props.value(QStringLiteral("offset"), QStringLiteral("0")).toString());
            const double frequency = number(c.props.value(QStringLiteral("frequency"), QStringLiteral("1k")).toString());
            nets.resize(3);
            nets << 2 << 'V' << -8 << 8 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << floatBits(1) << floatBits(frequency)
                 << floatBits(offset) << floatBits(amplitude);
            sine = {1, int(std::lround(frequency)), int(std::lround(offset)), int(std::lround(amplitude))};
            sine.resize(20);
            const QString g = QStringLiteral("%1").arg(amplitude, 0, 'g', 6);
            value = QStringLiteral("%1/%2V").arg(QString::number(offset - amplitude, 'g', 6), QString::number(offset + amplitude, 'g', 6));
            spice = QStringLiteral("%D %1 %2 DC 0 SIN(") + QString::number(offset, 'g', 6) + u' ' + g + u' '
                  + QString::number(frequency, 'g', 6) + QStringLiteral(" 0 0) AC 1 0");
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
            pkg << l.packageMap.mid(1 + n * d.part, n) << l.packageMap.mid(1 + n) << 0;
        } else {
            QVector<int> own = l.packageMap.mid(1, n);
            own.resize(n);
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
        const int flags = (l.flags & ~0x300) | 0x10 | (showDesignator ? 0x200 : 0) | (showLabel ? 0x100 : 0);

        int group = 0;
        if (l.parts > 1) {
            const auto it = packageIds.constFind(c.designator);
            group = it != packageIds.cend() ? *it : int(packageIds.size()) + 1;
            packageIds.insert(c.designator, group);
        }

        QStringList rec;
        rec << str(name);
        rec << join({l.type, d.origin.x(), d.origin.y(), 0, pinMax, std::max(int(pkg.size()) - 1, pinMax + 1)});
        rec << join(nets);
        if (l.type == 0xc3) rec << QString::number(sine.size()) << join(sine);
        rec << QStringLiteral("0");
        rec << join({0, 0, flags, cmAngle(c)});
        rec << str(value);
        if (!value.isEmpty()) rec << rect(textRect(value, c.labelAt, labelAnchor));
        rec << str(designator);
        if (!designator.isEmpty()) rec << rect(textRect(designator, c.designatorAt, designatorAnchor));
        rec << QStringLiteral("0") << str(l.power) << str(spice) << QStringLiteral("0") << str(modelKind) << str(l.package);
        rec << QString::number(pkg.size() - 1) << join(pkg);
        rec << join({l.code & 0xffff, 0, 0, 0, l.parts, l.parts > 1 ? d.part : 0, group, 0});
        rec << str(l.prefix);
        rec << join({i + 1, 0, 0});
        rec << QStringLiteral("2") << QStringLiteral("%1 0").arg(oleDate()) << QStringLiteral("0");
        if (!data.isEmpty()) rec << hexBytes(data);
        records << rec.join(u'\n');
    }
    rep.devices = int(devices.size());

    // ---- wires: each end on a device pin or on another wire
    QVector<const Wire*> wires;
    QVector<ItemId> wireIds;
    for (auto it = circuit.wires().cbegin(); it != circuit.wires().cend(); ++it) {
        if (it->bus) {
            rep.skipped << QStringLiteral("bus %1").arg(it->name);
            continue;
        }
        if (it->points.size() < 2) continue;
        wires << &*it;
        wireIds << it.key();
    }
    QStringList wireRecords;
    for (int i = 0; i < wires.size(); ++i) {
        const Wire& w = *wires[i];
        QVector<QPoint> pts;
        for (QPointF p : w.points) pts << toCm(p);
        int pin[2] = {0, 0}, dev[2] = {0, 0}, onWire[2] = {0, 0};
        for (int e = 0; e < 2; ++e) {
            const QPointF end = e == 0 ? w.points.first() : w.points.last();
            bool found = false;
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
            for (int j = 0; j < wires.size() && !found; ++j) {
                if (j == i || !WireGeometry::onPolyline(end, wires[j]->points)) continue;
                found = true;
                onWire[e] = j + 1;
            }
        }
        const int net = netNumber(netlist.wireNet.value(wireIds[i], -1));
        QStringList rec;
        rec << join({pin[0], pin[1], net, 0, 0, 0, 0, dev[0], dev[1], onWire[0], onWire[1], int(pts.size())});
        for (QPoint p : pts) rec << join({p.x(), p.y()});
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
        const QPoint at = toCm(t.pos);
        const QRect box(at, QSize(std::max(1, longest * height / 2) + 6, int(lines.size()) * height + 6));
        textRecords << join({height, height / 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 54}) + u'\n' + str(QStringLiteral("Courier New"))
                           + QStringLiteral("\n0 0 0 1\n") + rect(box) + u'\n' + rect(box.adjusted(3, 2, -3, -2)) + u'\n'
                           + str(t.text);
    }

    // ---- header: the view over everything placed
    QRect extent;
    for (const Device& d : devices) extent |= QRect(d.origin - QPoint(40, 40), QSize(80, 80));
    if (extent.isEmpty()) extent = QRect(0, 0, 600, 300);
    QStringList out{QString::fromLatin1(kHeaderStart)};
    out << QStringLiteral("0 0 1 100 10") << rect(extent);
    out << str(QStringLiteral("5.000 V")) << str(QStringLiteral("5.000 V")) << str(QStringLiteral("GND"));
    out << QStringLiteral("0 0") << QStringLiteral("24 100 0 0 0") << str(QStringLiteral("Package,Description,"))
        << QStringLiteral("0") << QStringLiteral("0 7") << QStringLiteral("0 4 0.500000 0.500000") << rect(extent)
        << QStringLiteral("42991634 0");
    const TitleBlock& tb = circuit.titleBlock();
    out << QStringLiteral("0") << str(tb.title) << str(tb.author) << QStringLiteral("0") << QStringLiteral("0") << QStringLiteral("0");
    out << QString::number(records.size());
    out << records;
    out << QString::number(wireRecords.size());
    out << wireRecords;
    out << QString::number(textRecords.size());
    out << textRecords;
    out << trailer();
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
