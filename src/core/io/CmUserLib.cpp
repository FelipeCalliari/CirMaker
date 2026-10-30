#include "core/io/CmUserLib.h"

#include "core/io/CmDeviceLib.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <cstring>

namespace cm {

namespace {

constexpr int kRecord = 0xf2;           // a device as CircuitMaker holds it
constexpr int kWire = 72;               // a wire: 0x38 bytes and four ints
constexpr int kMacroType = 0x5e;

class Reader {
public:
    explicit Reader(const QByteArray& d) : m_d(d) {}
    bool has(qsizetype at, qsizetype n) const { return at >= 0 && n >= 0 && at + n <= m_d.size(); }
    qint32 i32(qsizetype at) const
    {
        qint32 v = 0;
        if (has(at, 4)) std::memcpy(&v, m_d.constData() + at, 4);
        return v;
    }
    // a NUL-terminated string of at most n bytes
    QString str(qsizetype at, qsizetype n) const
    {
        if (!has(at, 1)) return {};
        qsizetype end = at;
        while (end < m_d.size() && end - at < n && m_d[end] != 0) ++end;
        return QString::fromLatin1(m_d.constData() + at, end - at);
    }
    QByteArray mid(qsizetype at, qsizetype n) const { return m_d.mid(at, n); }

private:
    const QByteArray& m_d;
};

// One device record at `at` (after its library name); `end` receives the
// offset after its arrays. False when it does not hold together.
bool readDevice(const Reader& r, qsizetype at, ckt::Device& d, qsizetype& end)
{
    if (!r.has(at, kRecord)) return false;
    d.type = r.i32(at);
    d.x = r.i32(at + 8);
    d.y = r.i32(at + 0xc);
    d.flags = r.i32(at + 0x3c);
    d.angle = r.i32(at + 0x40);
    const int nets = r.i32(at + 0x14), pkg = r.i32(at + 0x8c);
    if (d.type < 0 || d.type >= 1000 || nets < 0 || nets > 400 || pkg < 0 || pkg > 400) return false;
    qsizetype p = at + kRecord;
    if (!r.has(p, 4 * qsizetype(nets + pkg))) return false;
    d.nets.resize(nets);
    for (int k = 0; k < nets; ++k) d.nets[k] = r.i32(p + 4 * k);
    p += 4 * qsizetype(nets + pkg);
    QByteArray blob;
    if (r.i32(at + 0x88) != 0) {
        const int n = r.i32(p);
        if (n < 0 || n > 65536 || !r.has(p + 4, n)) return false;
        blob = r.mid(p + 4, n);
        p += 4 + n;
    }
    auto text = [&blob](int offset) -> QString {
        if (offset <= 0 || offset >= blob.size()) return {};
        const qsizetype stop = blob.indexOf('\0', offset);
        return QString::fromLatin1(blob.mid(offset, (stop < 0 ? blob.size() : stop) - offset));
    };
    auto rect = [&r, at](int off) {
        return QRectF(QPointF(r.i32(at + off), r.i32(at + off + 4)), QPointF(r.i32(at + off + 8), r.i32(at + off + 12)));
    };
    d.value = text(r.i32(at + 0x54));
    if (!d.value.isEmpty()) d.valueRect = rect(0x44);
    d.designator = text(r.i32(at + 0x68));
    if (!d.designator.isEmpty()) d.designatorRect = rect(0x58);
    d.tag = text(r.i32(at + 0x7c));
    d.prefix = r.str(at + 0x94, 8);
    if (const int n = r.i32(at + 0xd6); n > 0) {           // ints
        if (n > 4000) return false;
        p += 4 * qsizetype(n);
    }
    if (const int n = r.i32(at + 0xde); n > 0) {           // doubles
        if (n > 4000) return false;
        p += 8 * qsizetype(n);
    }
    if (const int n = r.i32(at + 0xe6); n > 0) {           // counted strings
        if (n > 400) return false;
        for (int k = 0; k < n; ++k) {
            const int len = r.i32(p);
            if (len < 0 || len > 65536) return false;
            p += 4 + len;
        }
    }
    if (d.type == 0x50) {                                   // PROM: its 32 bytes
        if (!r.has(p, 32)) return false;
        for (int k = 0; k < 32; ++k) d.data << quint8(r.mid(p + k, 1).at(0));
        p += 32;
    } else if (d.type == 0xaa) {
        return false;                                       // data sequencer contents: not read
    }
    end = p;
    return r.has(at, end - at);
}

// The circuit of the macro whose own record (library name `name`) starts at
// `at`, preceded by the device count.
bool readBody(const Reader& r, qsizetype at, const QString& name, CmMacro& m)
{
    const int devices = r.i32(at - 4);
    if (devices < 1 || devices > 2000) return false;
    QVector<ckt::Device> all;
    qsizetype p = at;
    for (int k = 0; k < devices; ++k) {
        ckt::Device d;
        d.name = r.str(p, 15);
        qsizetype end = 0;
        if (d.name.isEmpty() || !readDevice(r, p + 15, d, end)) return false;
        all << d;
        p = end;
    }
    if (all.first().type != kMacroType || all.first().name.compare(name, Qt::CaseInsensitive) != 0) return false;
    const int wires = r.i32(p);
    p += 4;
    if (wires < 0 || wires > 20000 || !r.has(p, qsizetype(wires) * kWire)) return false;
    QVector<ckt::CktWire> list(wires);
    for (int k = 0; k < wires; ++k) {
        const qsizetype w = p + qsizetype(k) * kWire;
        // as in a .ckt wire record: pins, net, flags, devices, wires the
        // ends lie on; device 1 is the macro itself, left out below
        auto dev = [](int v) { return v > 1 ? v - 1 : 0; };
        const int devA = r.i32(w + 56), devB = r.i32(w + 60);
        list[k].h = {devA == 1 ? 0 : r.i32(w), devB == 1 ? 0 : r.i32(w + 4), 0, 0, 0, 0, 0,
                     dev(devA), dev(devB), r.i32(w + 64), r.i32(w + 68), 0};
    }
    p += qsizetype(wires) * kWire;
    for (ckt::CktWire& w : list) {
        const int n = r.i32(p);
        p += 4;
        if (n < 0 || n > 10000 || !r.has(p, qsizetype(n) * 12)) return false;
        for (int k = 0; k < n; ++k) w.points << QPointF(r.i32(p + 12 * k), r.i32(p + 12 * k + 4));
        p += qsizetype(n) * 12;
        w.h[11] = n;
        if (n == 0) w.points << QPointF();
    }

    m.pinNets = all.first().nets.mid(1);
    m.body.version = 5.6;
    m.body.devices = all.mid(1);
    m.body.wires = list;
    return true;
}

} // namespace

bool CmUserLib::load(const QString& path, QString* error)
{
    m_macros.clear();
    CmDeviceLib headers;
    if (!headers.load(path, error)) return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    const QByteArray data = f.readAll();
    const Reader r(data);
    for (const CmLibDevice& h : headers.devices()) {
        if (h.type != kMacroType) continue;
        CmMacro m;
        m.name = h.name;
        // the pins in use: up to the last named or placed one
        int count = int(h.pins.size());
        while (count > 0 && h.pins[count - 1].isNull() && h.pinNames.value(count - 1).isEmpty()) --count;
        QSet<QString> seen;
        for (int k = 0; k < count; ++k) {
            QString n = h.pinNames.value(k).trimmed();
            if (n.isEmpty()) n = QString::number(k + 1);
            // repeated names would join different nets
            for (int s = 2; seen.contains(n); ++s) n = h.pinNames.value(k).trimmed() + u'_' + QString::number(s);
            seen << n;
            m.pins << n;
            m.outputs << (h.pins[k].x() > 0);
            m.at << h.pins[k];
        }
        // the circuit: the macro's own record under its library name
        const QByteArray key = h.name.toLatin1() + '\0';
        for (qsizetype at = data.indexOf(key); at >= 0; at = data.indexOf(key, at + 1)) {
            if (r.i32(at + 15) != kMacroType || r.str(at + 15 + 0x24, 16) != h.name) continue;
            CmMacro body = m;
            if (readBody(r, at, h.name, body)) {
                m = body;
                m_macros << m;
                break;
            }
        }
    }
    if (m_macros.isEmpty() && error) *error = QStringLiteral("no macros found");
    return !m_macros.isEmpty();
}

const CmMacro* CmUserLib::find(const QString& name) const
{
    auto bare = [](QString s) {
        s = s.trimmed();
        if (s.endsWith(u'~')) s.chop(1);
        return s;
    };
    for (const CmMacro& m : m_macros)
        if (bare(m.name).compare(bare(name), Qt::CaseInsensitive) == 0) return &m;
    return nullptr;
}

QString CmUserLib::locate(const QString& configuredFolder)
{
    const QString lib = CmDeviceLib::locate(configuredFolder);
    if (lib.isEmpty()) return {};
    const QString user = QFileInfo(lib).absolutePath() + QStringLiteral("/User.lib");
    return QFileInfo::exists(user) ? user : QString();
}

} // namespace cm
