#include "core/io/CmDeviceLib.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <cstring>

namespace cm {

namespace {

constexpr int kHeader = 0xfe;

class Bytes {
public:
    explicit Bytes(const QByteArray& d) : m_d(d) {}
    bool has(qsizetype at, qsizetype n) const { return at >= 0 && n >= 0 && at + n <= m_d.size(); }
    qint32 i32(qsizetype at) const
    {
        qint32 v;
        std::memcpy(&v, m_d.constData() + at, 4);
        return v;
    }
    qint16 i16(qsizetype at) const
    {
        qint16 v;
        std::memcpy(&v, m_d.constData() + at, 2);
        return v;
    }
    // A NUL-terminated printable name of 2..40 characters.
    QString name(qsizetype at) const
    {
        qsizetype end = at;
        while (end < m_d.size() && end - at <= 40 && m_d[end] >= 0x20 && m_d[end] < 0x7f) ++end;
        if (end >= m_d.size() || m_d[end] != 0 || end - at < 2 || end - at > 40) return {};
        return QString::fromLatin1(m_d.constData() + at, end - at);
    }
    QByteArray mid(qsizetype at, qsizetype n) const { return m_d.mid(at, n); }

private:
    const QByteArray& m_d;
};

// The record whose header starts at L, or false when L is not one.
bool parseRecord(const Bytes& b, qsizetype L, CmLibDevice& out)
{
    if (!b.has(L, kHeader)) return false;
    const int type = b.i32(L), pinTotal = b.i32(L + 0x20), mapCount = b.i32(L + 0x94);
    if (type <= 0 || type >= 1000 || pinTotal < 1 || pinTotal > 200 || mapCount < 0 || mapCount > 400) return false;
    const QString name = b.name(L + 0xc);
    if (name.isEmpty()) return false;
    qsizetype p = L + kHeader;
    QVector<QPoint> pins;
    QStringList names;
    for (int k = 0; k < pinTotal; ++k) {
        if (!b.has(p, 0x18)) return false;
        const QPoint at(b.i32(p), b.i32(p + 4));
        const int len = b.i32(p + 0x14);
        p += 0x18;
        if (len < 0 || len > 64 || !b.has(p, len)) return false;
        if (k == 0 && len != 0) return false;           // pin 0 is an unnamed placeholder
        names << QString::fromLatin1(b.mid(p, len));
        p += len;
        pins << at;
    }
    QVector<int> map;
    if (mapCount > 0) {
        if (!b.has(p, 4 * qsizetype(mapCount))) return false;
        for (int k = 0; k < mapCount; ++k) map << b.i32(p + 4 * k);
        p += 4 * qsizetype(mapCount);
    }
    if (const int c6 = b.i32(L + 0xc6); c6 > 0) {
        if (c6 > 4000 || !b.has(p, 4 * qsizetype(c6))) return false;
        p += 4 * qsizetype(c6);
    }
    QByteArray blob;
    if (b.i32(L + 0x90) != 0) {
        if (!b.has(p, 4)) return false;
        const int n = b.i32(p);
        if (n < 0 || n > 65536 || !b.has(p + 4, n)) return false;
        blob = b.mid(p + 4, n);
    }
    auto at = [&blob](int offset) -> QString {
        if (offset <= 0 || offset >= blob.size()) return {};
        const qsizetype end = blob.indexOf('\0', offset);
        return QString::fromLatin1(blob.mid(offset, (end < 0 ? blob.size() : end) - offset));
    };
    const int valueAt = b.i16(L + 0x5c), kindAt = b.i16(L + 0xd2), packageAt = b.i16(L + 0xd6);

    out = {};
    out.name = name;
    out.type = type;
    out.pinCount = std::max(0, pinTotal - 2);
    out.pins = pins.mid(1, out.pinCount);
    out.pinNames = names.mid(1, out.pinCount);
    out.packageMap = map;
    out.parts = b.i16(L + 0xb6);
    out.code = b.i16(L + 0xa4);
    out.flags = b.i32(L + 0x48);
    out.valueRect = QRect(QPoint(b.i32(L + 0x4c), b.i32(L + 0x50)), QPoint(b.i32(L + 0x54), b.i32(L + 0x58)));
    out.prefix = b.name(L + 0x9c);
    if (out.prefix.isEmpty() && b.has(L + 0x9c, 2) && b.i16(L + 0x9c) > 0x20 && b.i16(L + 0x9c) < 0x7f)
        out.prefix = QChar(b.i16(L + 0x9c));
    out.value = at(valueAt);
    out.kind = at(kindAt);
    out.package = at(packageAt);
    // power pins and SPICE template come first, before the strings located above
    QStringList leading;
    for (int o = 1; o < blob.size() && o != valueAt && o != kindAt && o != packageAt && blob[o] != 0;) {
        const QString s = at(o);
        leading << s;
        o += int(s.size()) + 1;
    }
    if (leading.size() >= 2) {
        out.power = leading[0];
        out.spice = leading[1];
    } else if (!leading.isEmpty()) {
        (leading[0].contains(u'%') ? out.spice : out.power) = leading[0];
    }
    return true;
}

} // namespace

bool CmDeviceLib::load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    const QByteArray data = f.readAll();
    const Bytes b(data);
    m_devices.clear();
    m_byName.clear();
    m_byValue.clear();
    // every NUL-terminated name can be the name field of a header 0xc bytes earlier
    for (qsizetype i = 0xc; i + 2 < data.size(); ++i) {
        if (data[i] < 0x20 || data[i] >= 0x7f) continue;
        CmLibDevice d;
        if (!parseRecord(b, i - 0xc, d)) continue;
        const QString key = d.name.toLower();
        if (m_byName.contains(key)) continue;
        m_byName.insert(key, int(m_devices.size()));
        m_devices << d;
    }
    // part numbers: the shortest device name wins ("2-In NAND~" over "2-In NAND:DM~")
    for (int i = 0; i < m_devices.size(); ++i) {
        const QString v = m_devices[i].value.toLower();
        if (v.isEmpty()) continue;
        const auto it = m_byValue.constFind(v);
        if (it == m_byValue.cend() || m_devices[*it].name.size() > m_devices[i].name.size())
            m_byValue.insert(v, i);
    }
    if (m_devices.isEmpty() && error) *error = QStringLiteral("no CircuitMaker devices found");
    return !m_devices.isEmpty();
}

const CmLibDevice* CmDeviceLib::find(const QString& name) const
{
    const auto it = m_byName.constFind(name.toLower());
    return it == m_byName.cend() ? nullptr : &m_devices[*it];
}

const CmLibDevice* CmDeviceLib::findByValue(const QString& value) const
{
    const auto it = m_byValue.constFind(value.toLower());
    return it == m_byValue.cend() ? nullptr : &m_devices[*it];
}

QString CmDeviceLib::locate(const QString& configuredFolder)
{
    QStringList folders;
    if (!configuredFolder.isEmpty()) folders << configuredFolder;
    const QString home = QDir::homePath();
    QString prefix = qEnvironmentVariable("WINEPREFIX");
    if (prefix.isEmpty()) prefix = home + QStringLiteral("/.wine");
    for (const QString& programs : {QStringLiteral("Program Files"), QStringLiteral("Program Files (x86)")})
        folders << prefix + QStringLiteral("/drive_c/") + programs + QStringLiteral("/CircuitMaker 2000");
    folders << QStringLiteral("C:/Program Files/CircuitMaker 2000") << QStringLiteral("C:/Program Files (x86)/CircuitMaker 2000");
    for (const QString& folder : std::as_const(folders)) {
        const QString file = QDir(folder).filePath(QStringLiteral("Device.lib"));
        if (QFileInfo::exists(file)) return file;
        // a configured path may name the file itself
        if (QFileInfo(folder).isFile()) return folder;
    }
    return {};
}

} // namespace cm
